#pragma once
/* Native AirPlay 2: CoreUtils encryption, PTP, peer management, transient pairing.
 * No persistent HomeKit pairing, classic RSA, FairPlay or AAC-ELD promises. */
#ifdef FT_AP_WITH_AAC
#ifdef FT_AP_WITH_BUFFERED
#define FT_AP_FEATURES_HI 0x18340U
#define FT_AP_FEATURES_TXT "0x401C0A00,0x18340"
#else
#define FT_AP_FEATURES_HI 0x18240U
#define FT_AP_FEATURES_TXT "0x401C0A00,0x18240"
#endif
#define FT_AP_FEATURES_LO 0x401C0A00U
#define FT_AP_AUDIO_FORMATS 0xD48800U
#else
#define FT_AP_FEATURES_HI 0x18240U
#define FT_AP_FEATURES_LO 0x400C0A00U
#define FT_AP_FEATURES_TXT "0x400C0A00,0x18240"
#define FT_AP_AUDIO_FORMATS 0x148800U /* PCM/ALAC, 44.1/48k, stereo16 */
#endif
