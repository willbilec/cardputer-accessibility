"""Generate a guarded experimental dependency; leave the upstream checkout clean."""
from pathlib import Path
import hashlib,json,shutil,subprocess
from urllib.request import urlopen
ROOT=Path(__file__).resolve().parent
UPSTREAM=ROOT.parent/'BackgroundAudio'
COMMIT='94641100866beae2cc97dc925c0d4562ad168879'
SONIC_COMMIT='b93885dcb70aae50c6f76b0fe4e0868f029a077e'
SONIC_PINS={
 'sonic.c':'4a21d8086f844e3e68cb4f85961623a69d35da8ca93cf6ea704ccd125e64eaf5',
 'sonic.h':'a2fc087b68c25141e2fb7ab56e71b111bee79cf311fcc850d492eb574c0eeffe',
 'LICENSE':'cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30',
}

def prepare_sonic(output):
 cache=ROOT/'generated/sonic-upstream'
 cache.mkdir(parents=True,exist_ok=True)
 for name,digest in SONIC_PINS.items():
  source=cache/name
  if not source.exists():
   url=f'https://raw.githubusercontent.com/waywardgeek/sonic/{SONIC_COMMIT}/{name}'
   with urlopen(url,timeout=20) as response:
    data=response.read()
   if hashlib.sha256(data).hexdigest()!=digest:
    raise ValueError(f'Unexpected Sonic source digest for {name}')
   source.write_bytes(data)
  else:
   data=source.read_bytes()
   if hashlib.sha256(data).hexdigest()!=digest:
    raise ValueError(f'Changed cached Sonic source {name}')
  if name=='sonic.c':
   data=(b'#if defined(UNIFIED_NVDA_REMOTE) || defined(UNIFIED_BIBLE_TEMPO)\n'+data+b'\n'
         b'void cardputer_sonic_discard(sonicStream stream) {\n'
         b'  stream->numInputSamples = stream->numOutputSamples = stream->numPitchSamples = 0;\n'
         b'  stream->inputPlayTime = stream->timeError = 0.0f;\n'
         b'  stream->oldRatePosition = stream->newRatePosition = 0;\n'
         b'  stream->prevPeriod = stream->prevMinDiff = 0;\n'
         b'}\n#endif\n')
  target=output/('LICENSE.sonic' if name=='LICENSE' else 'src/libespeak-ng/'+name)
  if not target.exists() or target.read_bytes()!=data:
   target.write_bytes(data)
PINS={
 'src/BackgroundAudioSpeech.h':'57e89298cbe3891cb32ff6861d5fa851b76b09f42f9b86781f430ae67e2a0cdf',
 'src/libespeak-ng/synthesize.h':'208c9ca8b412c350356aea59dd7fa75aecca44274d35d075e3833df7e007fc9f',
 'src/BackgroundAudioMixer.h':'5d6898dbea785a9a929de587ed6882359c0035d10f46ba4f814a5f53bcf0fa4a',
 'src/libespeak-ng/phonemelist.c':'543e29a7646b5bd411c1b857847fc74acc5bb22b878adbced4b3c72fbc2859f1',
 'src/libespeak-ng/translate.c':'330ac994d9de27dcbba35fde3881968d40f46f57a8998701e437d8ff046455b2',
 'src/libespeak-ng/wavegen.c':'32f46aeef43709ba8edf706f6e71b7e32505feb37f811e70d9337192ea973efa',
 'src/libespeak-ng/speech.c':'922cb8405a08025654155446da51797febb6259a5b99e4b42e787211ba43affe',
 'src/libespeak-ng/phoneme.h':'97001227a7c4469a2659ac9b38b03255af3b9db05a86dae19e106d59b17e9ae9',
 'src/libespeak-ng/phoneme/phontab.h':'14b827b18cc3cc878894dffcccf2f5c024e2fca7d2e7651c6351a835013b7781',
}
def prepare():
 assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=UPSTREAM,text=True).strip()==COMMIT,'Wrong BackgroundAudio revision'
 assert not subprocess.check_output(['git','status','--porcelain'],cwd=UPSTREAM,text=True).strip(),'Upstream has edits; refusing adaptation'
 for name,digest in PINS.items():
  assert hashlib.sha256((UPSTREAM/name).read_bytes().replace(b'\r\n',b'\n')).hexdigest()==digest,name
 output=ROOT/'generated/backgroundaudio-trial'
 marker=output/'adaptation.json'
 changes=[('src/BackgroundAudioSpeech.h','_out->setBuffers(5, framelen);','_out->setBuffers(1, framelen);'),
          ('src/BackgroundAudioSpeech.h',
           '    void setGain(float scale) {',
           '    void enablePeakLimiter() { _limitPeaks = true; }\n\n    void setGain(float scale) {'),
          ('src/BackgroundAudioSpeech.h',
           '    int32_t _gain = 1 << 16;',
           '    int32_t _gain = 1 << 16;\n    bool _limitPeaks = false;'),
          ('src/BackgroundAudioSpeech.h',
           '            ApplyGain(_frame, _frameLen * 2, _gain);',
           '            ApplyGain(_frame, _frameLen * 2, _gain);\n'
           '            if (_limitPeaks) {\n'
           '                // Round off loud peaks before the DAC. Leave ordinary speech alone.\n'
           '                constexpr int32_t knee = 17920, headroom = 24576 - knee;\n'
           '                for (int i = 0; i < _frameLen * 2; i += 2) {\n'
           '                    const int32_t sample = _frame[i];\n'
           '                    const int32_t excess = (sample < 0 ? -sample : sample) - knee;\n'
           '                    if (excess <= 0) continue;\n'
           '                    const int32_t peak = knee + headroom * excess / (headroom + excess);\n'
           '                    _frame[i] = _frame[i + 1] = sample < 0 ? -peak : peak;\n'
           '                }\n'
           '            }'),
          ('src/BackgroundAudioSpeech.h','        espeak_SetVoiceByFile("INTERNAL");',
           '        if (samplerate <= 0 || espeak_SetVoiceByFile("INTERNAL") != EE_OK) {\n            espeak_Terminate();\n            return false;\n        }'),
          ('src/BackgroundAudioSpeech.h',
           '        _generatingSpeech = false;\n        short *mono;\n        espeak_SynthesizeOneStep(&mono); // Thrown out\n        espeak_AbortSynthesis();',
           '        _generatingSpeech = false;\n        _frameLen = 0;\n        espeak_AbortSynthesis();'),
          # Trim only exact digital silence at the start of each utterance.
          ('src/BackgroundAudioSpeech.h','                        _generatingSpeech = true;',
           '                        _leadingSilence = 551; // At most 25 ms at 22.05 kHz.\n'
           '                        _generatingSpeech = true;'),
          ('src/BackgroundAudioSpeech.h','            // Now convert to stereo',
           '            while (_leadingSilence && _frameLen && *mono == 0) {\n'
           '                ++mono;\n'
           '                --_frameLen;\n'
           '                --_leadingSilence;\n'
           '            }\n'
           '            if (_frameLen) _leadingSilence = 0;\n'
           '            // Now convert to stereo'),
          ('src/BackgroundAudioSpeech.h','    int _frameLen = 0;',
           '    int _frameLen = 0;\n    int _leadingSilence = 0;'),
          ('src/libespeak-ng/synthesize.h','#define N_PHONEME_LIST 1000','#define N_PHONEME_LIST 250'),
          # A clause transition can refill the wave queue without producing
          # samples on its first call. Try again before writing silence.
          ('src/BackgroundAudioSpeech.h',
           '                generateOneFrame();\n            }\n            if (_paused || !_frameLen) {',
           '                generateOneFrame();\n#ifdef UNIFIED_PLAYPALACE\n'
           '                for (unsigned step = 0; !_frameLen && _generatingSpeech && step < 3; ++step) {\n'
           '                    generateOneFrame();\n                }\n#endif\n'
           '            }\n#if defined(UNIFIED_NVDA_REMOTE) && defined(UNIFIED_DIAGNOSTICS)\n'
           '            if (!_paused) {\n'
           '                _synthesizedSamples += _frameLen;\n'
           '                if (!_frameLen && _generatingSpeech) ++_emptyFramesWhileSpeaking;\n'
           '            }\n#endif\n'
           '            if (_paused || !_frameLen) {'),
          ('src/BackgroundAudioSpeech.h',
           '    bool done() {',
           '#if defined(UNIFIED_NVDA_REMOTE) && defined(UNIFIED_DIAGNOSTICS)\n'
           '    uint32_t synthesizedSamples() const { return _synthesizedSamples; }\n'
           '    uint32_t emptyFramesWhileSpeaking() const { return _emptyFramesWhileSpeaking; }\n'
           '#endif\n\n    bool done() {'),
          ('src/BackgroundAudioSpeech.h',
           '    // Quality stats, cumulative',
           '#if defined(UNIFIED_NVDA_REMOTE) && defined(UNIFIED_DIAGNOSTICS)\n'
           '    uint32_t _synthesizedSamples = 0;\n'
           '    uint32_t _emptyFramesWhileSpeaking = 0;\n'
           '#endif\n\n    // Quality stats, cumulative'),
          ('src/libespeak-ng/speech.c',
           '#include "wavegen.h"',
           '#ifdef UNIFIED_NVDA_REMOTE\n#include "sonic.h"\n#endif\n#include "wavegen.h"'),
          ('src/libespeak-ng/speech.c',
           'ESPEAK_NG_API int espeak_SynthesizeOneStep(short **out) {',
           '#ifdef UNIFIED_NVDA_REMOTE\n'
           'static sonicStream playbackTempo;\n'
           'static bool playbackTempoDraining;\n'
           'void cardputer_sonic_discard(sonicStream stream);\n'
           'ESPEAK_NG_API bool espeak_SetPlaybackTempo(float ratio) {\n'
           '        playbackTempoDraining = false;\n'
           '        if (ratio <= 1.0f) {\n'
           '                if (playbackTempo) sonicDestroyStream(playbackTempo);\n'
           '                playbackTempo = NULL;\n'
           '                return true;\n'
           '        }\n'
           '        if (!playbackTempo) playbackTempo = sonicCreateStream(22050, 1);\n'
           '        if (!playbackTempo) return false;\n'
           '        cardputer_sonic_discard(playbackTempo);\n'
           '        sonicSetSpeed(playbackTempo, ratio);\n'
           '        return true;\n'
           '}\n'
           'ESPEAK_NG_API float espeak_GetPlaybackTempo(void) {\n'
           '        return playbackTempo ? sonicGetSpeed(playbackTempo) : 1.0f;\n'
           '}\n'
           '#endif\n\n'
           'ESPEAK_NG_API int espeak_SynthesizeOneStep(short **out) {'),
          ('src/libespeak-ng/speech.c',
           '        event_list_ix = 0;\n        WavegenFill();\n\n        length = (out_ptr - outbuf)/2;\n        count_samples += length;\n\n        *out = (short *)outbuf;',
           '        event_list_ix = 0;\n'
           '#ifdef UNIFIED_NVDA_REMOTE\n'
           '        if (playbackTempoDraining) {\n'
           '                *out = (short *)outbuf;\n'
           '                return sonicReadShortFromStream(playbackTempo, *out, outbuf_size / 2);\n'
           '        }\n'
           '#endif\n'
           '        WavegenFill();\n\n        length = (out_ptr - outbuf)/2;\n        count_samples += length;\n'
           '#ifdef UNIFIED_NVDA_REMOTE\n'
           '        if (playbackTempo && length) {\n'
           '                if (sonicWriteShortToStream(playbackTempo, (short *)outbuf, length))\n'
           '                        length = sonicReadShortFromStream(playbackTempo, (short *)outbuf, outbuf_size / 2);\n'
           '                else espeak_SetPlaybackTempo(1.0f);\n'
           '        }\n'
           '#endif\n\n        *out = (short *)outbuf;'),
          ('src/libespeak-ng/speech.c',
           'ESPEAK_NG_API void espeak_AbortSynthesis() {\n        SpeakNextClause(2); // stop',
           'ESPEAK_NG_API void espeak_AbortSynthesis() {\n'
           '#ifdef UNIFIED_NVDA_REMOTE\n'
           '        playbackTempoDraining = false;\n'
           '        if (playbackTempo) cardputer_sonic_discard(playbackTempo);\n'
           '#endif\n        SpeakNextClause(2); // stop'),
          ('src/libespeak-ng/speech.c',
           'ESPEAK_NG_API int espeak_SynthesisGenerateNext() {\n        if (Generate',
           'ESPEAK_NG_API int espeak_SynthesisGenerateNext() {\n'
           '#ifdef UNIFIED_NVDA_REMOTE\n'
           '        if (playbackTempoDraining) {\n'
           '                if (sonicSamplesAvailable(playbackTempo)) return 1;\n'
           '                playbackTempoDraining = false;\n'
           '                return 0;\n'
           '        }\n'
           '#endif\n        if (Generate'),
          ('src/libespeak-ng/speech.c',
           '                        if (SpeakNextClause(1) == 0) {\n                                return 0;',
           '                        if (SpeakNextClause(1) == 0) {\n'
           '#ifdef UNIFIED_NVDA_REMOTE\n'
           '                                if (playbackTempo) {\n'
           '                                        if (!sonicFlushStream(playbackTempo)) espeak_SetPlaybackTempo(1.0f);\n'
           '                                        else if (sonicSamplesAvailable(playbackTempo)) {\n'
           '                                                playbackTempoDraining = true;\n'
           '                                                return 1;\n'
           '                                        }\n'
           '                                }\n'
           '#endif\n                                return 0;'),
          # The bundled phoneme bank in flash already holds the padded names.
          ('src/libespeak-ng/phoneme.h','\tchar name[N_PHONEME_TAB_NAME];',
           '#ifdef UNIFIED_NVDA_REMOTE\n\tconst char *name;\n#else\n\tchar name[N_PHONEME_TAB_NAME];\n#endif'),
          ('src/libespeak-ng/synthdata.c','\t\tmemcpy(phoneme_tab_list[ix].name, p, N_PHONEME_TAB_NAME);',
           '#ifdef UNIFIED_NVDA_REMOTE\n\t\tphoneme_tab_list[ix].name = (const char *)p;\n#else\n'
           '\t\tmemcpy(phoneme_tab_list[ix].name, p, N_PHONEME_TAB_NAME);\n#endif'),
          ('src/libespeak-ng/dictionary.c','\t\tp = phoneme_tab_list[plist->tone_ph].name;\n\t\tsprintf(phon_out, "(%s)", p);',
           '\t\tsprintf(phon_out, "(%s)", phoneme_tab_list[plist->tone_ph].name);'),
          # AudioOutputBase/Print promises bytes; upstream mixer returns uint32_t words.
          ('src/BackgroundAudioMixer.h','return avail;','return avail * sizeof(uint32_t);'),
          # begin() must retain the source rate configured by speech before begin.
          ('src/BackgroundAudioMixer.h','setFrequency(_outputRate);','setFrequency(_inputRate);'),
          ('src/BackgroundAudioMixer.h','#include <vector>','#include <vector>\n#include <atomic>\n#include <new>'),
          ('src/BackgroundAudioMixer.h',
           '            auto ab = new AudioBuffer;\n            ab->buff = new uint32_t[_wordsPerBuffer];',
           '            auto ab = new (std::nothrow) AudioBuffer;\n'
           '            if (ab) ab->buff = new (std::nothrow) uint32_t[_wordsPerBuffer];\n'
           '            if (!ab || !ab->buff) {\n'
           '                delete ab;\n'
           '                while (_empty) _deleteAudioBuffer(_takeFromList(&_empty));\n'
           '                return false;\n'
           '            }'),
          ('src/BackgroundAudioMixer.h','private:\n    void _addToList',
           '    void setLinearResampling(bool enabled) { _linearResampling = enabled; }\n'
           '    void discardBufferedAudio() { _discardPending = true; }\n'
           '    bool discardPending() const { return _discardPending.load(); }\n\nprivate:\n    void _addToList'),
          ('src/BackgroundAudioMixer.h','    uint32_t *getResampledBuffer() {',
           '    uint32_t *getResampledBuffer() {\n'
           '        // Apply a requested flush on the mixer task, the owner of the read cursor.\n'
           '        if (_discardPending.load()) {\n'
           '            while (_filled) _addToList(&_empty, _takeFromList(&_filled));\n'
           '            _readOff = 0;\n            _userOff = 0;\n'
           '            _discardPending = false;\n        }'),
          ('src/BackgroundAudioMixer.h',
           '                *(p++) = _filled ? _filled->buff[_readOff >> 16] : _silenceSample;',
           '                uint32_t frame = _silenceSample;\n'
           '                if (_filled) {\n'
           '                    const auto index = _readOff >> 16;\n'
           '                    frame = _filled->buff[index];\n'
           '                    const int32_t fraction = (_readOff & 0xffff) >> 1;\n'
           '                    if (_linearResampling && fraction) {\n'
           '                        const auto* next = index + 1 < _wordsPerBuffer ? &_filled->buff[index + 1]\n'
           '                                          : _filled->next ? _filled->next->buff : nullptr;\n'
           '                        if (next) {\n'
           '                            const auto lerp = [fraction](int16_t a, int16_t b) {\n'
           '                                return uint16_t(a + ((int32_t(b) - a) * fraction >> 15));\n'
           '                            };\n'
           '                            frame = uint32_t(lerp(frame >> 16, *next >> 16)) << 16 |\n'
           '                                    lerp(frame, *next);\n'
           '                        }\n'
           '                    }\n'
           '                }\n'
           '                *(p++) = frame;'),
          ('src/BackgroundAudioMixer.h','    bool _underflow;',
           '    bool _underflow;\n    bool _linearResampling = false;\n    std::atomic<bool> _discardPending{false};'),
          ('src/libespeak-ng/phonemelist.c',
           '#include "speech.h"',
           '#include "speech.h"\n#ifdef UNIFIED_NVDA_REMOTE\n#include <esp_attr.h>\n#endif'),
          ('src/libespeak-ng/translate.c',
           '#include "speech.h"',
           '#include "speech.h"\n#ifdef UNIFIED_NVDA_REMOTE\n#include <esp_attr.h>\n#endif'),
          ('src/libespeak-ng/phonemelist.c',
           'PHONEME_LIST *ph_list3; //[N_PHONEME_LIST]; // EFP3 - This would be 32K!!!  Use dynamic allocation\n        ph_list3 = (PHONEME_LIST *)malloc(N_PHONEME_LIST * sizeof(PHONEME_LIST));',
           '#ifdef UNIFIED_RESERVED_PHONEMES\n'
           '#ifdef UNIFIED_NVDA_REMOTE\n        RTC_FAST_ATTR\n#endif\n'
           '        static PHONEME_LIST reserved_ph_list3[N_PHONEME_LIST];\n'
           '        PHONEME_LIST *ph_list3 = reserved_ph_list3;\n'
           '#else\n        PHONEME_LIST *ph_list3 = (PHONEME_LIST *)malloc(N_PHONEME_LIST * sizeof(PHONEME_LIST));\n#endif'),
          ('src/libespeak-ng/phonemelist.c','free(ph_list3); // EFP3',
           '#ifndef UNIFIED_RESERVED_PHONEMES\n        free(ph_list3); // EFP3\n#endif'),
          # The unified reader serializes synthesis. Reserve its clause work
          # buffers so active Wi-Fi or TLS cannot turn a heap miss into a crash.
          ('src/libespeak-ng/translate.c',
           '        charix = (short *)malloc((N_TR_SOURCE+4) * sizeof(short));\n'
           '        words = (WORD_TAB *)malloc(N_CLAUSE_WORDS * sizeof(WORD_TAB));\n'
           '        sbuf = (char*)malloc(N_TR_SOURCE);',
           '#ifdef UNIFIED_PLAYPALACE\n'
           '        static short reserved_charix[N_TR_SOURCE+4];\n'
           '#ifdef UNIFIED_NVDA_REMOTE\n        RTC_SLOW_ATTR\n#endif\n'
           '        static WORD_TAB reserved_words[N_CLAUSE_WORDS];\n'
           '        static char reserved_sbuf[N_TR_SOURCE];\n'
           '        charix = reserved_charix;\n        words = reserved_words;\n        sbuf = reserved_sbuf;\n'
           '#else\n'
           '        charix = (short *)malloc((N_TR_SOURCE+4) * sizeof(short));\n'
           '        words = (WORD_TAB *)malloc(N_CLAUSE_WORDS * sizeof(WORD_TAB));\n'
           '        sbuf = (char*)malloc(N_TR_SOURCE);\n'
           '        if (!charix || !words || !sbuf) {\n'
           '            free(charix); free(words); free(sbuf);\n'
           '            return;\n'
           '        }\n'
           '#endif'),
          ('src/libespeak-ng/translate.c',
           '        num_wtab = (WORD_TAB *)malloc(N_CLAUSE_WORDS * sizeof(WORD_TAB));',
           '#ifdef UNIFIED_PLAYPALACE\n'
           '#ifdef UNIFIED_NVDA_REMOTE\n        RTC_SLOW_ATTR\n#endif\n'
           '        static WORD_TAB reserved_num_wtab[N_CLAUSE_WORDS];\n'
           '        num_wtab = reserved_num_wtab;\n'
           '#else\n'
           '        num_wtab = (WORD_TAB *)malloc(N_CLAUSE_WORDS * sizeof(WORD_TAB));\n'
           '        if (!num_wtab) {\n'
           '            free(sbuf); free(words); free(charix);\n'
           '            return;\n'
           '        }\n'
           '#endif'),
          ('src/libespeak-ng/translate.c','        free(num_wtab);',
           '#ifndef UNIFIED_PLAYPALACE\n        free(num_wtab);\n#endif'),
          ('src/libespeak-ng/translate.c','        free(sbuf);\n        free(words);\n        free(charix);',
           '#ifndef UNIFIED_PLAYPALACE\n        free(sbuf);\n        free(words);\n        free(charix);\n#endif'),
          # The fixed English voice uses dry speech. Keep its delay at zero
          # and reserve the 11 KB echo working set for connected workloads.
          ('src/libespeak-ng/synthesize.h', '#define N_ECHO_BUF 5500   // max of 250mS at 22050 Hz',
           '#ifdef UNIFIED_PLAYPALACE\n#define N_ECHO_BUF 1\n#else\n'
           '#define N_ECHO_BUF 5500   // max of 250mS at 22050 Hz\n#endif'),
          ('src/libespeak-ng/wavegen.c', '\tdelay = wvoice->echo_delay;\n\tamp = wvoice->echo_amp;',
           '#ifdef UNIFIED_PLAYPALACE\n\tdelay = 0;\n\tamp = 0;\n#else\n'
           '\tdelay = wvoice->echo_delay;\n\tamp = wvoice->echo_amp;\n#endif'),
          # The 22,050 Hz single-step reader and its mixer must agree with
          # espeak_ng_InitializeOutput's rounded audio-buffer size.
          ('src/libespeak-ng/speech.c',
           'static const int min_buffer_length = 60; // minimum buffer length in ms',
           '#ifdef UNIFIED_PLAYPALACE\nstatic const int min_buffer_length = 20;\n#else\n'
           'static const int min_buffer_length = 60; // minimum buffer length in ms\n#endif'),
          ('src/BackgroundAudioSpeech.h',
           '    static constexpr int framelen = 1324; // From the 22050 normal samplerate and 20 length',
           '#ifdef UNIFIED_PLAYPALACE\n    static constexpr int framelen = 442; // 20 ms, rounded up\n#else\n'
           '    static constexpr int framelen = 1324; // From the 22050 normal samplerate and 20 length\n#endif'),
          # All 134 tables in the pinned phontab remain available.
          ('src/libespeak-ng/phoneme.h',
           '#define N_PHONEME_TABS     150     // number of phoneme tables',
           '#ifdef UNIFIED_PLAYPALACE\n#define N_PHONEME_TABS     134\n#else\n'
           '#define N_PHONEME_TABS     150     // number of phoneme tables\n#endif')]
 adaptation=json.dumps({'upstream_commit':COMMIT,'sonic_commit':SONIC_COMMIT,
                        'version':'1.4.4','changes':changes},indent=2)+'\n'
 if not marker.exists() or marker.read_text(encoding='utf-8')!=adaptation:
  output.mkdir(parents=True,exist_ok=True)
  shutil.copytree(UPSTREAM/'src',output/'src',dirs_exist_ok=True)
  for name in ('library.json','library.properties','LICENSE'):shutil.copy2(UPSTREAM/name,output/name)
 for name,old,new in changes:
  path=output/name;text=path.read_text(encoding='utf-8')
  if new in text:
   assert text.count(new)==1,name
  elif old in text:
   assert text.count(old)==1,name
   path.write_text(text.replace(old,new),encoding='utf-8')
  else:raise AssertionError(name)
 prepare_sonic(output)
 marker.write_text(adaptation)
 return output
if __name__=='__main__':print(prepare())
