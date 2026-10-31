# FAAD2 application decoder import

Source: https://github.com/knik0/faad2
Commit: 3fbf62c5823d51b933d5127e8ea3b607d86f8f95
License: GPL-2.0-or-later, see COPYING and source notices.
Code from FAAD2 is copyright (c) Nero AG, www.nero.com

Imported libfaad and public include sources for the local experimental AirPlay
receiver. No ESP-IDF binaries, OS code or drivers are imported here.
The surrounding AirPlay reference has a separate non-commercial license;
this experimental combination is not a cleared distribution/commercial release.

Private build options: AAC-LC only, SBR/PS disabled, float arithmetic,
16-bit PCM output. Allocation goes through the application allocator adapter.
Read-only hcb11_2 and TNS coefficient arrays are const so they remain in flash
instead of consuming the constrained internal static-data region.

Validation: tools/freather/tests/test_airplay_aac.py generates raw AAC-LC packets
using an independent FFmpeg/PyAV encoder and exercises the production decoder
adapter at 44100/48000 Hz stereo. No copyrighted music fixture is included.
