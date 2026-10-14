// Sound on the Vita: the game's sound effects and music as MP3 files (converted from
// the user's own copy), decoded with mpg123 and mixed into one output port.
//   sound effects: <dir>/sfx/<linkage without extension>.mp3, decoded on first use and kept (LRU)
//   music:         <dir>/music/<name>.mp3, streamed
#ifndef CC_AUDIO_H
#define CC_AUDIO_H

int audio_init(const char *data_dir);   // 0 if there's no audio output
void audio_shutdown(void);

void audio_sfx_play(const char *linkage, float volume, float pan, int loops, const void *owner);
void audio_sfx_stop(const void *owner);   // what owner started; NULL: everything
void audio_music_play(const char *name, int loop);
void audio_music_stop(void);

#endif
