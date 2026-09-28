#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "cast_commands.h"
#include "cast_events.h"
#include "os/os_thread.h"

#define CHECK(condition)                                                                                                                                                                               \
    for (;;)                                                                                                                                                                                           \
    {                                                                                                                                                                                                  \
        if (!(condition))                                                                                                                                                                              \
        {                                                                                                                                                                                              \
            fprintf(stderr, "line %d: %s\n", __LINE__, #condition);                                                                                                                                    \
            return 1;                                                                                                                                                                                  \
        }                                                                                                                                                                                              \
        break;                                                                                                                                                                                         \
    }

typedef struct WorkerTest
{
    HANDLE        gate;
    volatile LONG started;
    volatile LONG completed;
} WorkerTest;

static void BlockedJob(void* pContext)
{
    WorkerTest* pTest = (WorkerTest*)pContext;

    (void)InterlockedIncrement(&pTest->started);
    (void)WaitForSingleObject(pTest->gate, 5000U);
    (void)InterlockedIncrement(&pTest->completed);
}

static int TestCommands(void)
{
    CastCommands_t      commands;
    unsigned            index;
    CastQueuedCommand_t command;

    CastCommands_Reset(&commands);
    for (index = 0U; index < 10000U; ++index)
    {
        CHECK(CastCommands_Push(&commands, CAST_COMMAND_SCRUB, (double)index));
    }
    CHECK(1U == OS_Queue_GetEntryCount(&commands.queue));
    CHECK(CastCommands_Push(&commands, CAST_COMMAND_SEEK, 12345.0));
    CHECK(CastCommands_Take(&commands, &command));
    CHECK((CAST_COMMAND_SEEK == command.command) && (12345.0 == command.seconds));
    CHECK(!CastCommands_Take(&commands, &command));
    CHECK(!CastCommands_Push(&commands, CAST_COMMAND_SEEK, -1.0));

    for (index = 0U; index < CAST_COMMAND_CAPACITY; ++index)
    {
        CHECK(CastCommands_Push(&commands, CAST_COMMAND_TOGGLE, 0.0));
    }
    CHECK(!CastCommands_Push(&commands, CAST_COMMAND_PLAY, 0.0));
    CHECK(CastCommands_Push(&commands, CAST_COMMAND_SEEK, 321.0));
    for (index = 0U; index < CAST_COMMAND_CAPACITY; ++index)
    {
        CHECK(CastCommands_Take(&commands, &command));
    }
    CHECK((CAST_COMMAND_SEEK == command.command) && (321.0 == command.seconds));
    CHECK(!CastCommands_Take(&commands, &command));

    for (index = 0U; index < CAST_COMMAND_CAPACITY; ++index)
    {
        CHECK(CastCommands_Push(&commands, CAST_COMMAND_TOGGLE, 0.0));
    }
    CHECK(CastCommands_Push(&commands, CAST_COMMAND_STOP, 0.0));
    CHECK(CastCommands_Push(&commands, CAST_COMMAND_STOP, 0.0));
    CHECK(!CastCommands_Push(&commands, CAST_COMMAND_PLAY, 0.0));
    CHECK(CastCommands_Take(&commands, &command));
    CHECK(CAST_COMMAND_STOP == command.command);
    CHECK(!CastCommands_Take(&commands, &command));
    CHECK(CastCommands_Push(&commands, CAST_COMMAND_STOP, 0.0));
    CHECK(!CastCommands_Take(&commands, &command));
    CastCommands_Reset(&commands);
    CHECK(CastCommands_Push(&commands, CAST_COMMAND_PLAY, 0.0));
    return 0;
}

static int TestEvents(void)
{
    CastEvents_t          events;
    CastEvents_Snapshot_t snapshot;
    CastStatus            status = {0};
    char     longText[CAST_EVENTS_TEXT_CAPACITY + 32U];
    unsigned index;

    CastEvents_Initialize(&events);
    CastEvents_Begin(&events, 1U);
    CHECK(!CastEvents_Take(&events, &snapshot));
    for (index = 0U; index < 10000U; ++index)
    {
        status.currentTime = (double)index;
        CHECK(CastEvents_Publish(&events, 1U, CAST_EVENT_STATUS, &status, NULL));
    }
    memset(longText, 'x', sizeof(longText));
    longText[sizeof(longText) - 1U] = '\0';
    CHECK(CastEvents_Publish(&events, 1U, CAST_EVENT_LOG, NULL, longText));
    CHECK(CastEvents_Publish(&events, 1U, CAST_EVENT_ENDED, NULL, "connection closed"));
    CHECK(!CastEvents_Publish(&events, 1U, CAST_EVENT_LOG, NULL, "late log"));
    CHECK(CastEvents_Take(&events, &snapshot));
    CHECK((CAST_EVENTS_STATUS | CAST_EVENTS_LOG | CAST_EVENTS_ENDED) == snapshot.present);
    CHECK(9999.0 == snapshot.status.currentTime);
    CHECK((CAST_EVENTS_TEXT_CAPACITY - 1U) == strlen(snapshot.log));
    CHECK(0 == strcmp(snapshot.reason, "connection closed"));
    CHECK(!CastEvents_Take(&events, &snapshot));
    CastEvents_Begin(&events, 2U);
    CHECK(!CastEvents_Publish(&events, 1U, CAST_EVENT_ENDED, NULL, "stale end"));
    CHECK(!CastEvents_Publish(&events, 2U, CAST_EVENT_STATUS, NULL, NULL));
    CHECK(CastEvents_Publish(&events, 2U, CAST_EVENT_LOG, NULL, "current"));
    CHECK(CastEvents_Take(&events, &snapshot));
    CHECK(CAST_EVENTS_LOG == snapshot.present);
    CHECK(0 == strcmp(snapshot.log, "current"));
    return 0;
}

static int TestWorker(void)
{
    WorkerTest context;
    OS_Thread_t worker;
    ULONGLONG  deadline;
    unsigned   index;

    context.started   = 0L;
    context.completed = 0L;
    context.gate      = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(NULL != context.gate);
    CHECK(OS_Thread_Initialize(&worker, BlockedJob, &context));
    CHECK(OS_Thread_Submit(&worker));
    CHECK(!OS_Thread_Submit(&worker));
    CHECK(!OS_Thread_TakeResult(&worker));
    CHECK(SetEvent(context.gate));
    deadline = GetTickCount64() + 5000U;
    for (;;)
    {
        if (OS_Thread_TakeResult(&worker))
        {
            break;
        }
        CHECK(GetTickCount64() < deadline);
        Sleep(1U);
    }
    CHECK(1L == InterlockedCompareExchange(&context.completed, 0L, 0L));
    for (index = 0U; index < 1000U; ++index)
    {
        CHECK(OS_Thread_Submit(&worker));
        deadline = GetTickCount64() + 5000U;
        for (;;)
        {
            if (OS_Thread_TakeResult(&worker))
            {
                break;
            }
            CHECK(GetTickCount64() < deadline);
            Sleep(1U);
        }
        CHECK((LONG)(index + 2U) == InterlockedCompareExchange(&context.completed, 0L, 0L));
    }
    CHECK(OS_Thread_Stop(&worker));
    CHECK(OS_Thread_Stop(&worker));
    CHECK(!OS_Thread_Submit(&worker));
    deadline = GetTickCount64() + 5000U;
    for (;;)
    {
        if (OS_Thread_TryClose(&worker))
        {
            break;
        }
        CHECK(GetTickCount64() < deadline);
        Sleep(1U);
    }
    CHECK(OS_Thread_TryClose(&worker));
    CHECK(OS_Thread_TryClose(&worker));
    CHECK(CloseHandle(context.gate));
    return 0;
}

int main(void)
{
    CHECK(0 == TestCommands());
    CHECK(0 == TestEvents());
    CHECK(0 == TestWorker());
    puts("control saturation, stale events and persistent worker checks passed");
    return 0;
}
