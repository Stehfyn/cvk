#include "audio_gain.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

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
        int needs = AudioGain_NeedsConversion(argv[1], error, sizeof(error));
        if (needs < 0) { fprintf(stderr, "%s\n", error); return 1; }
        printf("source requires AAC conversion: %d\n", needs);
        if (!AudioGain_Convert(argv[1], argv[2], (unsigned)_wtoi(argv[3]), &cancel, &progress, error, sizeof(error)))
        { fprintf(stderr, "%s\n", error); return 1; }
        CHECK(progress == 100);
        CHECK(AudioGain_NeedsConversion(argv[2], error, sizeof(error)) == 0);
        {
            wchar_t cancelledPath[1024];
            FILE* partial;
            CHECK(swprintf(cancelledPath, 1024, L"%ls.cancel.mp4", argv[2]) > 0);
            cancel = 1; progress = 0;
            CHECK(!AudioGain_Convert(argv[1], cancelledPath, 100, &cancel, &progress, error, sizeof(error)));
            CHECK(strstr(error, "cancelled") != NULL);
            partial = _wfopen(cancelledPath, L"rb");
            if (partial) fclose(partial);
            CHECK(partial == NULL);
        }
        puts("audio conversion completed");
    }
    else CHECK(argc == 1);
    puts("audio gain checks passed");
    return 0;
}
