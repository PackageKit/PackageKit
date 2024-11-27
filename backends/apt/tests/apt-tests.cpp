#include "gst-matcher.h"

const char *gst_plugins_bad_pkg = R"(Package: gstreamer1.0-plugins-bad
Architecture: amd64
Version: 1.24.8-2ubuntu1
Multi-Arch: same
Priority: extra
Section: universe/libs
Source: gst-plugins-bad1.0
Origin: Ubuntu
Maintainer: Ubuntu Developers <ubuntu-devel-discuss@lists.ubuntu.com>
Original-Maintainer: Maintainers of GStreamer packages <gst-plugins-bad1.0@packages.debian.org>
Bugs: https://bugs.launchpad.net/ubuntu/+filebug
Installed-Size: 11020
Provides: gstreamer1.0-audiosink, gstreamer1.0-audiosource, gstreamer1.0-plugins-bad-faad, gstreamer1.0-plugins-bad-videoparsers, gstreamer1.0-videosink, gstreamer1.0-videosource, gstreamer1.0-visualization
Depends: gstreamer1.0-plugins-base (>= 1.24.0), gstreamer1.0-plugins-good (>= 1.24.0), libgstreamer-plugins-bad1.0-0 (= 1.24.8-2ubuntu1), libaom3 (>= 3.2.0), libass9 (>= 1:0.13.6), libavtp0 (>= 0.2.0), libbs2b0 (>= 3.1.0+dfsg), libbz2-1.0, libc6 (>= 2.38), libcairo2 (>= 1.6.0), libchromaprint1 (>= 1.3.2), libcurl3t64-gnutls (>= 7.55.0), libdc1394-25 (>= 2.2.6), libdca0 (>= 0.0.5), libde265-0 (>= 0.9), libdirectfb-1.7-7t64 (>= 1.7.7), libdrm2 (>= 2.4.98), libdvdnav4 (>= 4.1.3), libdvdread8t64 (>= 4.1.3), libfaad2 (>= 2.7), libflite1 (>= 1.4-release-9~), libfluidsynth3 (>= 2.2.0), libfreeaptx0 (>= 0.1.1), libgcc-s1 (>= 3.3.1), libglib2.0-0t64 (>= 2.80.0), libgme0 (>= 0.6.0), libgsm1 (>= 1.0.18), libgstreamer-gl1.0-0 (>= 1.24.0), libgstreamer-plugins-base1.0-0 (>= 1.24.0), libgstreamer-plugins-good1.0-0 (>= 1.24.7), libgstreamer1.0-0 (>= 1.24.0), libgtk-3-0t64 (>= 3.15.0), libgudev-1.0-0 (>= 146), libimath-3-1-29t64 (>= 3.1.11), libjson-glib-1.0-0 (>= 1.5.2), liblc3-1 (>= 1.0.1), liblcms2-2 (>= 2.7), libldacbt-enc2 (>= 2.0.2), liblilv-0-0 (>= 0.22), liblrdf0 (>= 0.4.0-1.2), libltc11 (>= 1.2.0), libmjpegutils-2.1-0t64 (>= 1:2.1.0+debian), libmodplug1 (>= 1:0.8.8.5), libmpcdec6 (>= 1:0.1~r435), libmpeg2encpp-2.1-0t64 (>= 1:2.1.0+debian), libmplex2-2.1-0t64 (>= 1:2.1.0+debian), libneon27t64, libnettle8t64 (>= 3), libopenal1 (>= 1:1.14), libopenexr-3-1-30 (>= 3.1.5), libopenh264-7 (>= 2.4.1+dfsg), libopenjp2-7 (>= 2.2.0), libopenmpt0t64 (>= 0.5.10), libopenni2-0 (>= 2.2.0.33+dfsg), libopus0 (>= 1.1), liborc-0.4-0t64 (>= 1:0.4.34), libpango-1.0-0 (>= 1.22.0), libpangocairo-1.0-0 (>= 1.22), libqrencode4 (>= 3.2.0), librsvg2-2 (>= 2.36.2), librtmp1 (>= 2.3), libsbc1 (>= 2.0), libsndfile1 (>= 1.0.20), libsoundtouch1 (>= 2.0.0), libspandsp2t64 (>= 0.0.6~pre18), libsrt1.5-gnutls (>= 1.5.3), libsrtp2-1 (>= 2.0.0+20170516), libssl3t64 (>= 3.0.0), libstdc++6 (>= 13.1), libsvtav1enc2 (>= 2.1.0+dfsg), libusb-1.0-0 (>= 2:1.0.8), libva2 (>= 2.2.0), libvo-aacenc0 (>= 0.1.3), libvo-amrwbenc0 (>= 0.1.3), libvulkan1 (>= 1.2.131.2), libwayland-client0 (>= 1.20.0), libwebp7 (>= 1.4.0), libwebpmux3 (>= 1.4.0), libwebrtc-audio-processing-1-3 (>= 1.3), libwildmidi2 (>= 0.2.3), libx11-6, libx265-209 (>= 3.6), libxml2 (>= 2.9.0), libzbar0t64 (>= 0.10), libzvbi0t64 (>= 0.2.35), libzxing3 (>= 2.2.1)
Suggests: frei0r-plugins
Conflicts: gstreamer1.0-plugins-bad-faad (<< 1.11.91-1ubuntu1), gstreamer1.0-plugins-bad-videoparsers (<< 1.11.91-1ubuntu1)
Breaks: gstreamer1.0-plugins-base (<< 0.11.94), gstreamer1.0-plugins-good (<< 1.1.2)
Replaces: gstreamer1.0-plugins-bad-faad (<< 1.11.91-1ubuntu1), gstreamer1.0-plugins-bad-videoparsers (<< 1.11.91-1ubuntu1), gstreamer1.0-plugins-base (<< 0.11.94), gstreamer1.0-plugins-good (<< 1.1.2)
Filename: pool/universe/g/gst-plugins-bad1.0/gstreamer1.0-plugins-bad_1.24.8-2ubuntu1_amd64.deb
Size: 3042084
MD5sum: 6ce2fdec6c7ddc9077d8580a19e19b2c
SHA1: 979c1d9ffd177d92124e43325f2d1fdf9fc110d1
SHA256: c4038572cd32da9e8e5d5e4f0949accc9cc15a3fabe1b8ef6573dba47e2ea524
SHA512: 6689c1da1a6b399742e71874f625f1794ebc0ce6e3cb1a57e78fb881fa8794f22dab6e363907805c4bd9eba9a0d6afcb82520f066a1a8b105c740467e067684b
Homepage: https://gstreamer.freedesktop.org
Description: GStreamer plugins from the "bad" set
Task: ubuntustudio-desktop, ubuntukylin-desktop, ubuntukylin-desktop, ubuntukylin-desktop-minimal, ubuntu-budgie-desktop, ubuntu-budgie-desktop-raspi, ubuntu-unity-desktop, ubuntucinnamon-desktop-minimal, ubuntucinnamon-desktop
Gstreamer-Decoders: application/dash+xml; application/mxf; application/vnd.ms-sstr+xml; application/x-hls; application/x-yuv4mpeg, y4mversion=(int)2; audio/midi; audio/mpeg, mpegversion=(int)4, stream-format=(string){ raw, adts }; audio/mpeg, mpegversion=(int)2; audio/ms-gsm; audio/riff-midi; audio/x-adpcm, layout=(string){ microsoft, dvi }; audio/x-aiff; audio/x-ay; audio/x-dts; audio/x-gbs; audio/x-gsm; audio/x-gym; audio/x-hes; audio/x-ircam; audio/x-it; audio/x-kss; audio/x-midi-event; audio/x-mod; audio/x-musepack, streamversion=(int){ 7, 8 }; audio/x-nist; audio/x-nsf; audio/x-paris; audio/x-private1-dts; audio/x-rf64; audio/x-s3m; audio/x-sap; audio/x-sbc, parsed=(boolean)true; audio/x-sds; audio/x-siren, dct-length=(int)320; audio/x-spc; audio/x-stm; audio/x-svx; audio/x-vgm; audio/x-voc; audio/x-w64; audio/x-xi; audio/x-xm; image/jp2; image/png; image/svg; image/svg+xml; image/webp; image/x-exr; image/x-j2c; image/x-jpc; image/x-jpc-striped; image/x-portable-anymap; image/x-portable-bitmap; image/x-portable-graymap; image/x-portable-pixmap; video/mpeg, mpegversion=(int){ 1, 2 }, systemstream=(boolean){ true, false }; video/mpeg, mpegversion=(int)4, systemstream=(boolean)false; video/mpegts, systemstream=(boolean)true; video/x-av1; video/x-cdxa; video/x-divx, divxversion=(int)[ 4, 5 ]; video/x-h263, variant=(string)itu; video/x-h264; video/x-h265; video/x-ivf; video/x-vmnc, version=(int)1; video/x-vp8, codec-alpha=(boolean)true; video/x-vp9
Gstreamer-Elements: a2dpsink, accurip, adpcmdec, adpcmenc, aesdec, aesenc, aiffmux, aiffparse, alphacombine, asfmux, asfparse, assrender, atscmux, audiobuffersplit, audiochannelmix, audiolatency, audiomixmatrix, audioparse, audiosegmentclip, autoconvert, autodeinterlace, autovideoconvert, autovideoflip, av12json, av1dec, av1enc, av1parse, avdtpsink, avdtpsrc, avtpaafdepay, avtpaafpay, avtpcrfcheck, avtpcrfsync, avtpcvfdepay, avtpcvfpay, avtprvfdepay, avtprvfpay, avtpsink, avtpsrc, avwait, bayer2rgb, bpmdetect, bs2b, bulge, burn, bz2dec, bz2enc, cc708overlay, cccombiner, ccconverter, ccextractor, cea608mux, checksumsink, chopmydata, chromahold, chromaprint, chromium, circle, clockselect, codecalphademux, coloreffects, combdetect, compare, curlfilesink, curlftpsink, curlhttpsink, curlhttpsrc, curlsftpsink, curlsmtpsink, dashdemux, dashsink, dc1394src, debugqroverlay, debugspy, decklinkaudiosink, decklinkaudiosrc, decklinkvideosink, decklinkvideosrc, dfbvideosink, diffuse, dilate, diracparse, dodge, dtlsdec, dtlsenc, dtlssrtpdec, dtlssrtpdemux, dtlssrtpenc, dtmfdetect, dtsdec, dvbbasebin, dvbsrc, dvbsubenc, dvbsuboverlay, dvdspu, errorignore, exclusion, faad, faceoverlay, fakeaudiosink, fakevideosink, fbdevsink, festival, fieldanalysis, fisheye, flitetestsrc, fluiddec, fpsdisplaysink, freeverb, gaussianblur, gdpdepay, gdppay, gmedec, gsmdec, gsmenc, gtkwaylandsink, h263parse, h2642json, h264parse, h264timestamper, h2652json, h265parse, h265timestamper, hlsdemux, hlssink, hlssink2, id3mux, insertbin, interaudiosink, interaudiosrc, interlace, intersubsink, intersubsrc, intervideosink, intervideosrc, ipcpipelinesink, ipcpipelinesrc, ipcslavepipeline, irtspparse, ivfparse, ivtc, jp2kdecimator, jpeg2000parse, kaleidoscope, kmssink, ladspa-amp-so-amp-mono, ladspa-amp-so-amp-stereo, ladspa-delay-so-delay-5s, ladspa-filter-so-hpf, ladspa-filter-so-lpf, ladspa-sine-so-sine-faaa, ladspa-sine-so-sine-faac, ladspa-sine-so-sine-fcaa, ladspasrc-noise-so-noise-white, ladspasrc-sine-so-sine-fcac, lc3dec, lc3enc, lcms, ldacenc, libde265dec, line21decoder, line21encoder, marble, midiparse, mirror, modplug, mpeg2enc, mpeg4videoparse, mpegpsdemux, mpegpsmux, mpegtsmux, mpegvideoparse, mplex, msesrc, mssdemux, musepackdec, mxfdemux, mxfmux, neonhttpsrc, netsim, objectdetectionoverlay, openalsink, openalsrc, openaptxdec, openaptxenc, openexrdec, openh264dec, openh264enc, openjpegdec, openjpegenc, openmptdec, openni2src, opusparse, pcapparse, perspective, pinch, pitch, pngparse, pnmdec, pnmenc, proxysink, proxysrc, qroverlay, removesilence, rfbsrc, rgb2bayer, ristrtpdeext, ristrtpext, ristrtxreceive, ristrtxsend, ristsink, ristsrc, rotate, roundrobin, rsndvdbin, rsvgdec, rsvgoverlay, rtmp2sink, rtmp2src, rtmpsink, rtmpsrc, rtpasfpay, rtponvifparse, rtponviftimestamp, rtpsink, rtpsrc, sbcdec, sbcenc, scenechange, sctpdec, sctpenc, sdpdemux, sdpsrc, sfdec, shmsink, shmsrc, simplevideomark, simplevideomarkdetect, sirendec, sirenenc, smooth, solarize, spacescope, spanplc, spectrascope, speed, sphere, square, srtclientsink, srtclientsrc, srtenc, srtpdec, srtpenc, srtserversink, srtserversrc, srtsink, srtsrc, stretch, svtav1enc, switchbin, synaescope, teletextdec, testsrcbin, timecodestamper, tonegeneratesrc, transcodebin, tsdemux, tsparse, ttmlparse, ttmlrender, tunnel, twirl, unixfdsink, unixfdsrc, uritranscodebin, uvch264mjpgdemux, uvch264src, uvcsink, vc1parse, videoanalyse, videocodectestsink, videodiff, videoframe-audiolevel, videoparse, videosegmentclip, vmncdec, voaacenc, voamrwbenc, vp82json, vp8alphadecodebin, vp9alphadecodebin, vp9parse, vulkancolorconvert, vulkandownload, vulkanh264dec, vulkanh265dec, vulkanimageidentity, vulkanoverlaycompositor, vulkanshaderspv, vulkansink, vulkanupload, vulkanviewconvert, watchdog, waterripple, wavescope, waylandsink, webpdec, webpenc, webrtcbin, webrtcdsp, webrtcechoprobe, webvttenc, wildmididec, x265enc, y4mdec, zbar, zebrastripe, zxing
Gstreamer-Encoders: application/mxf; application/x-bzip; application/x-dtls; application/x-gdp; application/x-rtp, media=(string){ audio, video, application }, encoding-name=(string)X-ASF-PF; application/x-sctp; application/x-subtitle; application/x-subtitle-vtt; audio/AMR-WB; audio/aptx; audio/aptx-hd; audio/mpeg, mpegversion=(int)4, stream-format=(string){ adts, raw }, base-profile=(string)lc; audio/x-adpcm, layout=(string)dvi; audio/x-aiff; audio/x-gsm; audio/x-lc3, frame-bytes=(int)[ 20, 400 ], frame-duration-us=(int){ 10000, 7500 }, framed=(boolean)true; audio/x-ldac, channel-mode=(string){ mono, dual, stereo }; audio/x-sbc, channel-mode=(string){ mono, dual, stereo, joint }, blocks=(int){ 4, 8, 12, 16 }, subbands=(int){ 4, 8 }, allocation-method=(string){ snr, loudness }, bitpool=(int)[ 2, 64 ]; audio/x-siren, dct-length=(int)320; image/jp2; image/webp; image/x-j2c, num-components=(int)[ 1, 4 ], sampling=(string){ RGB, BGR, RGBA, BGRA, YCbCr-4:4:4, YCbCr-4:2:2, YCbCr-4:2:0, YCbCr-4:1:1, YCbCr-4:1:0, GRAYSCALE, YCbCrA-4:4:4:4 }, colorspace=(string){ sRGB, sYUV, GRAY }; image/x-jpc, num-components=(int)[ 1, 4 ], num-stripes=(int)[ 1, 2147483647 ], alignment=(string){ frame, stripe }, sampling=(string){ RGB, BGR, RGBA, BGRA, YCbCr-4:4:4, YCbCr-4:2:2, YCbCr-4:2:0, YCbCr-4:1:1, YCbCr-4:1:0, GRAYSCALE, YCbCrA-4:4:4:4 }, colorspace=(string){ sRGB, sYUV, GRAY }; image/x-jpc-striped, num-components=(int)[ 1, 4 ], sampling=(string){ RGB, BGR, RGBA, BGRA, YCbCr-4:4:4, YCbCr-4:2:2, YCbCr-4:2:0, YCbCr-4:1:1, YCbCr-4:1:0, GRAYSCALE, YCbCrA-4:4:4:4 }, colorspace=(string){ sRGB, sYUV, GRAY }, num-stripes=(int)[ 2, 2147483647 ], stripe-height=(int)[ 1, 2147483647 ]; image/x-portable-anymap; image/x-portable-bitmap; image/x-portable-graymap; image/x-portable-pixmap; video/mpeg, systemstream=(boolean)false, mpegversion=(int){ 1, 2 }; video/mpeg, systemstream=(boolean)true; video/mpegts, systemstream=(boolean)true, packetsize=(int){ 192, 188 }; video/x-av1, stream-format=(string)obu-stream, alignment=(string)tu; video/x-h264, stream-format=(string)byte-stream, alignment=(string)au, profile=(string){ constrained-baseline, baseline, main, constrained-high, high }; video/x-h265, stream-format=(string)byte-stream, alignment=(string)au, profile=(string){ main, main-still-picture, main-intra, main-444, main-444-intra, main-444-still-picture, main-10, main-10-intra, main-422-10, main-422-10-intra, main-444-10, main-444-10-intra, main-12, main-12-intra, main-422-12, main-422-12-intra, main-444-12, main-444-12-intra }; video/x-ms-asf, parsed=(boolean)true
Gstreamer-Uri-Sinks: rtmfp, rtmp, rtmpe, rtmps, rtmpt, rtmpte, rtmpts, rtp, srt
Gstreamer-Uri-Sources: dvb, dvd, http, https, mse, rfb, rist, rtmfp, rtmp, rtmpe, rtmps, rtmpt, rtmpte, rtmpts, rtp, sdp, srt, testbin
Gstreamer-Version: 1.24
Description-md5: 96aaaad9b842ce9ddb51b002cc05eca0
)";

const char *gst_plugins_ugly_pkg = R"(
Package: gstreamer1.0-plugins-ugly
Architecture: amd64
Version: 1.24.8-1
Multi-Arch: same
Priority: optional
Section: universe/libs
Source: gst-plugins-ugly1.0
Origin: Ubuntu
Maintainer: Ubuntu Developers <ubuntu-devel-discuss@lists.ubuntu.com>
Original-Maintainer: Maintainers of GStreamer packages <gst-plugins-ugly1.0@packages.debian.org>
Bugs: https://bugs.launchpad.net/ubuntu/+filebug
Installed-Size: 762
Depends: liba52-0.7.4 (>= 0.7.4), libc6 (>= 2.14), libcdio19t64 (>= 2.1.0), libdvdread8t64 (>= 4.1.3), libgcc-s1 (>= 3.3.1), libglib2.0-0t64 (>= 2.80.0), libgstreamer-plugins-base1.0-0 (>= 1.24.0), libgstreamer1.0-0 (>= 1.24.0), libmpeg2-4 (>= 0.5.1), liborc-0.4-0t64 (>= 1:0.4.34), libsidplay1v5, libstdc++6 (>= 5), libx264-164 (>= 2:0.164.3108+git31e19f9)
Filename: pool/universe/g/gst-plugins-ugly1.0/gstreamer1.0-plugins-ugly_1.24.8-1_amd64.deb
Size: 189710
MD5sum: 89b6e8f329891e6dcebbd6a39677223e
SHA1: 7b6976a3c521ca35d85e63fe8087ac06a052f2db
SHA256: c2ab817c21a54209c706c94b2bbc9116f26f0ba3f2816c6425b2c507f542aa18
SHA512: d93e976c6e328c0e6e4fd52ed91425b548f3a164dcb11a3ab8da041847b4e0375ff682178f4801d674c938acdf46dd6ab41d99b903f1d4fef52c451493c51d05
Homepage: https://gstreamer.freedesktop.org
Description: GStreamer plugins from the "ugly" set
Task: ubuntu-budgie-desktop-minimal, ubuntu-budgie-desktop, ubuntu-budgie-desktop-raspi, ubuntu-unity-desktop, ubuntucinnamon-desktop-minimal, ubuntucinnamon-desktop, ubuntucinnamon-desktop, ubuntucinnamon-desktop-raspi
Gstreamer-Decoders: application/vnd.rn-realmedia; application/x-pn-realaudio; application/x-rtp, media=(string){ application, video, audio }, payload=(int)[ 96, 127 ], encoding-name=(string)X-ASF-PF; audio/ac3; audio/x-ac3; audio/x-lpcm; audio/x-private-ts-lpcm; audio/x-private1-ac3; audio/x-private1-lpcm; audio/x-private2-lpcm; audio/x-sid; video/mpeg, mpegversion=(int)[ 1, 2 ], systemstream=(boolean)false; video/x-ms-asf
Gstreamer-Elements: a52dec, asfdemux, cdiocddasrc, dvdlpcmdec, dvdreadsrc, dvdsubdec, dvdsubparse, mpeg2dec, rademux, rmdemux, rtpasfdepay, rtspwms, siddec, x264enc
Gstreamer-Encoders: video/x-h264, stream-format=(string){ avc, byte-stream }, alignment=(string)au, profile=(string){ high-4:4:4, high-4:2:2, high-10, high, main, baseline, constrained-baseline, high-4:4:4-intra, high-4:2:2-intra, high-10-intra }
Gstreamer-Uri-Sources: cdda, dvd
Gstreamer-Version: 1.24
Description-md5: c036226562f55540aad2e51fbde63d54
)";

static GStrv
codec_strv(const char *codec)
{
    g_autoptr(GStrvBuilder) builder = g_strv_builder_new();
    g_strv_builder_add(builder, codec);
    return g_strv_builder_end(builder);
}

static void
apt_test_gst_matcher_bad_codec (void)
{
    {
        GstMatcher matcher(codec_strv("foobar()"));
        g_assert_false(matcher.hasMatches());
    }

    {
        GstMatcher matcher(codec_strv("foobar()()(64bit)"));
        g_assert_false(matcher.hasMatches());
    }
}

static void
apt_test_gst_matcher_with_caps (void)
{
    {
        /* Matches amd64-only */
        GstMatcher matcher(codec_strv("gstreamer1(decoder-audio/mpeg)(mpegversion=4)()(64bit)"));
        g_assert_true(matcher.hasMatches());

        g_assert_true(matcher.matches(gst_plugins_bad_pkg, "amd64"));
        g_assert_false(matcher.matches(gst_plugins_bad_pkg, "i386"));
    }

    {
        /* Matches any architectures */
        GstMatcher matcher(codec_strv("gstreamer1(decoder-audio/mpeg)(mpegversion=4)"));
        g_assert_true(matcher.hasMatches());

        g_assert_true(matcher.matches(gst_plugins_bad_pkg, "amd64"));
        g_assert_true(matcher.matches(gst_plugins_bad_pkg, "i386"));
        g_assert_true(matcher.matches(gst_plugins_bad_pkg, "arm64"));
    }

    {
        /* Matches the right package only */
        GstMatcher matcher(codec_strv("gstreamer1(decoder-audio/mpeg)(mpegversion=4)"));
        g_assert_true(matcher.hasMatches());

        g_assert_true(matcher.matches(gst_plugins_bad_pkg, "amd64"));
        g_assert_false(matcher.matches(gst_plugins_ugly_pkg, "amd64"));
        g_assert_false(matcher.matches("", "amd64"));
    }
}

static void
apt_test_gst_matcher_without_caps (void)
{
    {
        /* Matches amd64-only */
        GstMatcher matcher(codec_strv("gstreamer1(decoder-video/x-h265)()(64bit)"));
        g_assert_true(matcher.hasMatches());

        g_assert_true(matcher.matches(gst_plugins_bad_pkg, "amd64"));
        g_assert_false(matcher.matches(gst_plugins_bad_pkg, "i386"));
    }

    {
        /* Matches any architectures */
        GstMatcher matcher(codec_strv("gstreamer1(decoder-video/x-h265)"));
        g_assert_true(matcher.hasMatches());

        g_assert_true(matcher.matches(gst_plugins_bad_pkg, "amd64"));
        g_assert_true(matcher.matches(gst_plugins_bad_pkg, "i386"));
        g_assert_true(matcher.matches(gst_plugins_bad_pkg, "arm64"));
    }

    {
        /* Matches the right package only */
        GstMatcher matcher(codec_strv("gstreamer1(decoder-video/x-h265)"));
        g_assert_true(matcher.hasMatches());

        g_assert_true(matcher.matches(gst_plugins_bad_pkg, "amd64"));
        g_assert_false(matcher.matches(gst_plugins_ugly_pkg, "amd64"));
        g_assert_false(matcher.matches("", "amd64"));
    }
}

static void
apt_test_gst_matcher_bad_caps (void)
{
    {
        GstMatcher matcher(codec_strv("gstreamer1(decoder-audio/mpeg)(mpegversion=5)()(64bit)"));
        g_assert_true(matcher.hasMatches());

        g_assert_false(matcher.matches(gst_plugins_bad_pkg, "amd64"));
    }

    {
        GstMatcher matcher(codec_strv("gstreamer1(decoder-audio/mpeg)(mpegversion=5)"));
        g_assert_true(matcher.hasMatches());

        g_assert_false(matcher.matches(gst_plugins_bad_pkg, "amd64"));
    }
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    /* tests go here */
    g_test_add_func ("/apt/gst-matcher/bad-codec", apt_test_gst_matcher_bad_codec);
    g_test_add_func ("/apt/gst-matcher/with-caps", apt_test_gst_matcher_with_caps);
    g_test_add_func ("/apt/gst-matcher/without-caps", apt_test_gst_matcher_without_caps);
    g_test_add_func ("/apt/gst-matcher/bad-caps", apt_test_gst_matcher_bad_caps);

    return g_test_run();
}
