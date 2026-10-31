#include "rtsp_message.h"
#include "ft_airplay_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>

#include "esp_log.h"
#include "rtsp_crypto.h"

static const char *TAG = "rtsp_message";

const uint8_t *rtsp_find_header_end(const uint8_t *data, size_t len) {
  for (size_t i = 0; i + 3 < len; i++) {
    if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' &&
        data[i + 3] == '\n') {
      return data + i;
    }
  }
  return NULL;
}

int rtsp_parse_cseq(const char *request) {
  const char *cseq = strstr(request, "CSeq:");
  if (cseq) {
    return (int)strtol(cseq + 5, NULL, 10);
  }
  return 1;
}

int rtsp_parse_content_length(const char *request) {
  const char *cl = strcasestr(request, "\r\nContent-Length:");
  if (!cl) return 0;
  cl += 17;
  while (*cl == ' ' || *cl == '\t') ++cl;
  if (*cl < '0' || *cl > '9') return -1;
  size_t length = 0;
  while (*cl >= '0' && *cl <= '9') {
    if (length > (FT_AP_BODY_MAX - (unsigned)(*cl - '0')) / 10) return -1;
    length = length * 10 + (unsigned)(*cl++ - '0');
  }
  while (*cl == ' ' || *cl == '\t') ++cl;
  if (cl[0] != '\r' || cl[1] != '\n') return -1;
  return (int)length;
}

const uint8_t *rtsp_get_body(const char *request, size_t request_len,
                             size_t *body_len) {
  const char *body = strstr(request, "\r\n\r\n");
  if (body) {
    body += 4;
    *body_len = request_len - (body - request);
    return (const uint8_t *)body;
  }
  *body_len = 0;
  return NULL;
}

// Parse Transport header for client ports (AirPlay 1)
// Format: Transport:
// RTP/AVP/UDP;unicast;mode=record;control_port=6001;timing_port=6002
void rtsp_parse_transport(const char *request, uint16_t *control_port,
                          uint16_t *timing_port) {
  if (control_port) {
    *control_port = 0;
  }
  if (timing_port) {
    *timing_port = 0;
  }

  // RTSP header names and Transport-header parameter names are
  // case-insensitive (RFC 2326), so match accordingly.
  const char *transport = strcasestr(request, "Transport:");
  if (!transport) {
    return;
  }

  // Find end of Transport header line
  const char *line_end = strstr(transport, "\r\n");
  if (!line_end) {
    line_end = transport + strlen(transport);
  }

  // Parse control_port
  const char *cp = strcasestr(transport, "control_port=");
  if (cp && cp < line_end && control_port) {
    *control_port = (uint16_t)strtoul(cp + 13, NULL, 10);
  }

  // Parse timing_port
  const char *tp = strcasestr(transport, "timing_port=");
  if (tp && tp < line_end && timing_port) {
    *timing_port = (uint16_t)strtoul(tp + 12, NULL, 10);
  }
}

int rtsp_request_parse(const uint8_t *data, size_t len, rtsp_request_t *req) {
  ft_ap_request_t parsed;
  if (!req || ft_ap_request_parse(data, len, &parsed) != FT_AP_OK) return -1;
  memset(req, 0, sizeof(*req));
  memcpy(req->method, parsed.method, sizeof(req->method));
  memcpy(req->path, parsed.path, sizeof(req->path));
  strcpy(req->protocol, parsed.http ? "HTTP/1.1" : "RTSP/1.0");
  memcpy(req->content_type, parsed.content_type, sizeof(req->content_type));
  req->cseq = (int)parsed.cseq;
  req->content_length = parsed.body_bytes;
  req->body = parsed.body;
  req->body_len = parsed.body_bytes;
  return 0;
}

// Internal: send all data, handling partial sends
static int send_all(int socket, const uint8_t *data, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    ssize_t r = send(socket, data + sent, len - sent, 0);
    if (r <= 0) {
      return -1;
    }
    sent += (size_t)r;
  }
  return 0;
}

int rtsp_send_response(int socket, rtsp_conn_t *conn, int status_code,
                       const char *status_text, int cseq,
                       const char *extra_headers, const char *body,
                       size_t body_len) {
  char header[1024];
  int header_len;
  ESP_LOGI(TAG,"response code=%d cseq=%d",status_code,cseq);
  ap_diag_record("response",status_code);

  if (extra_headers && body && body_len > 0) {
    header_len =
        snprintf(header, sizeof(header),
                 "RTSP/1.0 %d %s\r\n"
                 "CSeq: %d\r\n"
                 "Server: AirTunes/377.40.00\r\n"
                 "%s"
                 "Content-Length: %u\r\n"
                 "\r\n",
                 status_code, status_text, cseq, extra_headers, (unsigned)body_len);
  } else if (extra_headers) {
    header_len = snprintf(header, sizeof(header),
                          "RTSP/1.0 %d %s\r\n"
                          "CSeq: %d\r\n"
                          "Server: AirTunes/377.40.00\r\n"
                          "%s"
                          "\r\n",
                          status_code, status_text, cseq, extra_headers);
  } else if (body && body_len > 0) {
    header_len = snprintf(header, sizeof(header),
                          "RTSP/1.0 %d %s\r\n"
                          "CSeq: %d\r\n"
                          "Server: AirTunes/377.40.00\r\n"
                          "Content-Length: %u\r\n"
                          "\r\n",
                          status_code, status_text, cseq, (unsigned)body_len);
  } else {
    header_len = snprintf(header, sizeof(header),
                          "RTSP/1.0 %d %s\r\n"
                          "CSeq: %d\r\n"
                          "Server: AirTunes/377.40.00\r\n"
                          "\r\n",
                          status_code, status_text, cseq);
  }

  // Build complete response
  if (header_len < 0 || (size_t)header_len >= sizeof(header) || body_len > FT_AP_BODY_MAX || (body_len && !body)) return -1;
  size_t total_len = (size_t)header_len + body_len;
  uint8_t *response = malloc(total_len);
  if (!response) {
    ESP_LOGE(TAG, "Failed to allocate response buffer");
    return -1;
  }

  memcpy(response, header, (size_t)header_len);
  if (body && body_len > 0) {
    memcpy(response + header_len, body, body_len);
  }

  // Send encrypted or plain depending on mode
  int result;
  if (conn && conn->encrypted_mode) {
    result = rtsp_crypto_write_frame(socket, conn, response, total_len);
  } else {
    result = (send_all(socket, response, total_len) < 0) ? -1 : 0;
    if (result < 0) {
      ESP_LOGE(TAG, "Failed to send RTSP response");
    }
  }

  free(response);
  return result;
}

int rtsp_send_ok(int socket, rtsp_conn_t *conn, int cseq) {
  return rtsp_send_response(socket, conn, 200, "OK", cseq, NULL, NULL, 0);
}

int rtsp_send_http_response(int socket, rtsp_conn_t *conn, int status_code,
                            const char *status_text, const char *content_type,
                            const char *body, size_t body_len) {
  char header[512];
  int header_len = snprintf(header, sizeof(header),
                            "HTTP/1.1 %d %s\r\n"
                            "Content-Type: %s\r\n"
                            "Content-Length: %u\r\n"
                            "Server: AirTunes/377.40.00\r\n"
                            "CSeq: 1\r\n"
                            "\r\n",
                            status_code, status_text, content_type, (unsigned)body_len);

  // Build complete response
  if (header_len < 0 || (size_t)header_len >= sizeof(header) || body_len > FT_AP_BODY_MAX || (body_len && !body)) return -1;
  size_t total_len = (size_t)header_len + body_len;
  uint8_t *response = malloc(total_len);
  if (!response) {
    ESP_LOGE(TAG, "Failed to allocate response buffer");
    return -1;
  }

  memcpy(response, header, (size_t)header_len);
  if (body && body_len > 0) {
    memcpy(response + header_len, body, body_len);
  }

  // Send encrypted or plain depending on mode
  int result;
  if (conn && conn->encrypted_mode) {
    result = rtsp_crypto_write_frame(socket, conn, response, total_len);
  } else {
    result = (send_all(socket, response, total_len) < 0) ? -1 : 0;
    if (result < 0) {
      ESP_LOGE(TAG, "Failed to send HTTP response");
    }
  }

  free(response);
  return result;
}
