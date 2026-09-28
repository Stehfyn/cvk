#include "audio_gain.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int wmain(int argc, wchar_t** argv)
{
    int16_t original[] = {-32768, -12000, -1, 0, 1, 12000, 32767};
    int16_t samples[7];
    memcpy(samples, original, sizeof(samples));
    AudioGain_Apply(samples, 7, 100);
    CHECK(!memcmp(samples, original, sizeof(samples)));
    AudioGain_Apply(samples, 7, 200);
    CHECK(samples[0] == -32768 && samples[1] == -24000 && samples[2] == -2 && samples[3] == 0 &&
          samples[4] == 2 && samples[5] == 24000 && samples[6] == 32767);
    AudioGain_Apply(samples, 7, 0);
    for (int i = 0; i < 7; ++i) CHECK(samples[i] == 0);
    if (argc == 4)
    {
        char error[512];
        volatile long cancel = 0, progress = 0;
        if (!AudioGain_Convert(argv[1], argv[2], (unsigned)_wtoi(argv[3]), &cancel, &progress, error, sizeof(error)))
        { fprintf(stderr, "%s\n", error); return 1; }
        CHECK(progress == 100);
        puts("audio conversion completed");
    }
    else CHECK(argc == 1);
    puts("audio gain checks passed");
    return 0;
}
