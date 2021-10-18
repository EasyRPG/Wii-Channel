#ifndef VIDEO_H
#define VIDEO_H

void InitVideo(void);
void DeinitVideo(void);
void StartRenderThread(bool wait);
void StopRenderThread(bool wait);
void SetProgress(int percent);

void EnableConsole(void);

#endif
