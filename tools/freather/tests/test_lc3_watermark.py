"""Compile the production watermark function against a deterministic clock."""
import re
import subprocess
from pathlib import Path
root = Path(__file__).resolve().parents[3]
source = (root / "projects/FeatherTalk_M55/applications/le_audio/lc3_decode_m55.c").read_text(encoding="utf-8")
start = source.index("rt_bool_t ft_lc3_watermark_tick(void)")
end = source.index("/* ---- LC3", start)
function = source[start:end]
defines = "\n".join(re.findall(r"(?m)^#define FT_(?:STARVE_HOLDOFF_MS|REPREBUF_BYTES)\s+\d+U", source))
prefix = """
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef int rt_bool_t;
typedef uint32_t rt_uint32_t;
#define RT_TRUE 1
#define RT_FALSE 0
#define FT_ALINK 0
#define rt_kprintf(...) ((void)0)
static int s_dec_ready=1,s_sound_open=1,s_out_active=1,s_starve_counted;
static uint32_t s_empty_since,s_stat_starves,now,used;
static uint32_t ft_alink_used(int unused) { (void)unused; return used; }
static uint32_t rt_tick_get(void) { return now; }
static uint32_t rt_tick_from_millisecond(uint32_t ms) { return ms; }
"""
test = """
int main(void) {
    /* 1000 small packets, each separated by an empty interval, never starve. */
    for (now=1; now<10000; now+=10) {
        used=0; assert(ft_lc3_watermark_tick());
        now+=1; used=246; assert(ft_lc3_watermark_tick()); now-=1;
    }
    assert(s_stat_starves==0 && s_empty_since==0);
    used=0; now=20000; assert(ft_lc3_watermark_tick());
    now+=FT_STARVE_HOLDOFF_MS; assert(!ft_lc3_watermark_tick());
    assert(s_stat_starves==1);
    used=246; assert(!ft_lc3_watermark_tick());
    used=FT_REPREBUF_BYTES; assert(ft_lc3_watermark_tick());
    assert(!s_starve_counted && !s_empty_since);
    puts("PASS: normal interpacket gaps, sustained starvation, rebuffer recovery");
}
"""
exe = Path(__file__).with_suffix(".exe")
subprocess.run(["gcc", "-x", "c", "-", "-o", str(exe)],
               input=prefix+defines+"\n"+function+test, text=True, check=True)
subprocess.run([str(exe)], check=True)
