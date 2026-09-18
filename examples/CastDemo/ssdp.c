/* ssdp.c - SSDP M-SEARCH over every IPv4 interface, then WinHTTP fetches of the device
** descriptions. The XML is scanned for friendlyName and modelName without a parser. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <winhttp.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ssdp.h"

#define MAX_SEARCH_SOCKETS (16)
#define LOCATION_CAPACITY  (256)

static const char kSearch[] = "M-SEARCH * HTTP/1.1\r\n"
                              "HOST: 239.255.255.250:1900\r\n"
                              "MAN: \"ssdp:discover\"\r\n"
                              "MX: 2\r\n"
                              "ST: urn:dial-multiscreen-org:service:dial:1\r\n"
                              "\r\n";

/* Copies the value of a header line, matched case-insensitively. */
static int GetHeader(const char* pResponse, const char* pName, char* pOut, size_t capacity)
{
    const size_t nameLength = strlen(pName);
    const char*  pLine      = pResponse;

    while (NULL != pLine && 0 != *pLine)
    {
        const char*  pEnd       = strstr(pLine, "\r\n");
        const size_t lineLength = (NULL != pEnd) ? (size_t)(pEnd - pLine) : strlen(pLine);

        if (lineLength > nameLength && ':' == pLine[nameLength] && 0 == _strnicmp(pLine, pName, nameLength))
        {
            const char* pValue = pLine + nameLength + 1;
            size_t      valueLength;

            while (pValue < pLine + lineLength && (' ' == *pValue || '\t' == *pValue))
            {
                ++pValue;
            }
            valueLength = (size_t)(pLine + lineLength - pValue);
            if (valueLength >= capacity)
            {
                valueLength = capacity - 1;
            }
            memcpy(pOut, pValue, valueLength);
            pOut[valueLength] = 0;
            return 1;
        }
        pLine = (NULL != pEnd) ? pEnd + 2 : NULL;
    }
    return 0;
}

/* Copies the text of the first <element>...</element>. */
static int GetElementText(const char* pXml, const char* pElement, char* pOut, size_t capacity)
{
    char        open[64];
    const char* pStart;
    const char* pEnd;
    size_t      length;

    (void)snprintf(open, sizeof(open), "<%s>", pElement);
    pStart = strstr(pXml, open);
    if (NULL == pStart)
    {
        pOut[0] = 0;
        return 0;
    }
    pStart += strlen(open);
    pEnd = strchr(pStart, '<');
    if (NULL == pEnd)
    {
        pOut[0] = 0;
        return 0;
    }
    length = (size_t)(pEnd - pStart);
    if (length >= capacity)
    {
        length = capacity - 1;
    }
    memcpy(pOut, pStart, length);
    pOut[length] = 0;
    return 1;
}

static int HttpGet(const char* pUrl, char* pOut, size_t capacity)
{
    WCHAR          url[512];
    WCHAR          host[256];
    WCHAR          path[512];
    URL_COMPONENTS parts;
    HINTERNET      hSession = NULL;
    HINTERNET      hConnect = NULL;
    HINTERNET      hRequest = NULL;
    DWORD          total    = 0;
    int            ok       = 0;

    if (0 == MultiByteToWideChar(CP_UTF8, 0, pUrl, -1, url, (int)(sizeof(url) / sizeof(url[0]))))
    {
        return 0;
    }
    ZeroMemory(&parts, sizeof(parts));
    parts.dwStructSize     = sizeof(parts);
    parts.lpszHostName     = host;
    parts.dwHostNameLength = (DWORD)(sizeof(host) / sizeof(host[0]));
    parts.lpszUrlPath      = path;
    parts.dwUrlPathLength  = (DWORD)(sizeof(path) / sizeof(path[0]));
    if (!WinHttpCrackUrl(url, 0, 0, &parts))
    {
        return 0;
    }
    hSession = WinHttpOpen(L"cvk CastDemo/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (NULL != hSession)
    {
        WinHttpSetTimeouts(hSession, 3000, 3000, 3000, 3000);
        hConnect = WinHttpConnect(hSession, host, parts.nPort, 0);
    }
    if (NULL != hConnect)
    {
        hRequest = WinHttpOpenRequest(hConnect, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    }
    if (NULL != hRequest && WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(hRequest, NULL))
    {
        DWORD read = 0;

        while (total < capacity - 1 && WinHttpReadData(hRequest, pOut + total, (DWORD)(capacity - 1 - total), &read) && read > 0)
        {
            total += read;
        }
        pOut[total] = 0;
        ok          = total > 0;
    }
    if (NULL != hRequest)
    {
        WinHttpCloseHandle(hRequest);
    }
    if (NULL != hConnect)
    {
        WinHttpCloseHandle(hConnect);
    }
    if (NULL != hSession)
    {
        WinHttpCloseHandle(hSession);
    }
    return ok;
}

/* "http://192.168.1.5:8008/ssdp/device-desc.xml" -> "192.168.1.5" */
static void HostFromUrl(const char* pUrl, char* pOut, size_t capacity)
{
    const char* pStart = strstr(pUrl, "://");
    const char* pEnd;
    size_t      length;

    pStart = (NULL != pStart) ? pStart + 3 : pUrl;
    pEnd   = pStart + strcspn(pStart, ":/");
    length = (size_t)(pEnd - pStart);
    if (length >= capacity)
    {
        length = capacity - 1;
    }
    memcpy(pOut, pStart, length);
    pOut[length] = 0;
}

static int Describe(const char* pLocation, CastDevice* pDevice)
{
    char* pXml = (char*)malloc(16384);
    int   ok   = 0;

    if (NULL == pXml)
    {
        return 0;
    }
    ZeroMemory(pDevice, sizeof(*pDevice));
    HostFromUrl(pLocation, pDevice->host, sizeof(pDevice->host));
    if (HttpGet(pLocation, pXml, 16384))
    {
        ok = GetElementText(pXml, "friendlyName", pDevice->friendlyName, sizeof(pDevice->friendlyName));
        GetElementText(pXml, "modelName", pDevice->model, sizeof(pDevice->model));
    }
    free(pXml);
    return ok;
}

/* One UDP socket per IPv4 interface, so the multicast search leaves every network. */
static unsigned OpenSearchSockets(SOCKET* pSockets, unsigned capacity)
{
    ULONG                 size     = 0;
    IP_ADAPTER_ADDRESSES* pAdapters;
    IP_ADAPTER_ADDRESSES* pAdapter;
    unsigned              count = 0;
    const ULONG           flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;

    if (ERROR_BUFFER_OVERFLOW != GetAdaptersAddresses(AF_INET, flags, NULL, NULL, &size) || 0 == size)
    {
        return 0;
    }
    pAdapters = (IP_ADAPTER_ADDRESSES*)malloc(size);
    if (NULL == pAdapters)
    {
        return 0;
    }
    if (NO_ERROR != GetAdaptersAddresses(AF_INET, flags, NULL, pAdapters, &size))
    {
        free(pAdapters);
        return 0;
    }
    for (pAdapter = pAdapters; NULL != pAdapter && count < capacity; pAdapter = pAdapter->Next)
    {
        IP_ADAPTER_UNICAST_ADDRESS* pUnicast;

        if (IfOperStatusUp != pAdapter->OperStatus || IF_TYPE_SOFTWARE_LOOPBACK == pAdapter->IfType)
        {
            continue;
        }
        for (pUnicast = pAdapter->FirstUnicastAddress; NULL != pUnicast && count < capacity; pUnicast = pUnicast->Next)
        {
            struct sockaddr_in local;
            SOCKET             s;

            if (AF_INET != pUnicast->Address.lpSockaddr->sa_family)
            {
                continue;
            }
            memcpy(&local, pUnicast->Address.lpSockaddr, sizeof(local));
            local.sin_port = 0;
            s              = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (INVALID_SOCKET == s)
            {
                continue;
            }
            if (0 != bind(s, (struct sockaddr*)&local, sizeof(local)))
            {
                closesocket(s);
                continue;
            }
            setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&local.sin_addr, sizeof(local.sin_addr));
            pSockets[count++] = s;
        }
    }
    free(pAdapters);
    return count;
}

unsigned Ssdp_DiscoverCastDevices(CastDevice* pDevices, unsigned capacity, unsigned timeoutMs)
{
    SOCKET             sockets[MAX_SEARCH_SOCKETS];
    char               locations[CAST_MAX_DEVICES][LOCATION_CAPACITY];
    unsigned           locationCount = 0;
    unsigned           deviceCount   = 0;
    unsigned           socketCount   = OpenSearchSockets(sockets, MAX_SEARCH_SOCKETS);
    struct sockaddr_in group;
    ULONGLONG          deadline;
    unsigned           index;
    unsigned           attempt;

    if (capacity > CAST_MAX_DEVICES)
    {
        capacity = CAST_MAX_DEVICES;
    }
    if (0 == socketCount)
    {
        return 0;
    }
    ZeroMemory(&group, sizeof(group));
    group.sin_family = AF_INET;
    group.sin_port   = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &group.sin_addr);
    for (attempt = 0; attempt < 2; ++attempt) /* twice, against loss on Wi-Fi */
    {
        for (index = 0; index < socketCount; ++index)
        {
            sendto(sockets[index], kSearch, (int)(sizeof(kSearch) - 1), 0, (struct sockaddr*)&group, sizeof(group));
        }
    }
    deadline = GetTickCount64() + timeoutMs;
    for (;;)
    {
        const ULONGLONG now = GetTickCount64();
        struct timeval  timeout;
        fd_set          readable;
        int             ready;

        if (now >= deadline || locationCount >= capacity)
        {
            break;
        }
        timeout.tv_sec  = (long)((deadline - now) / 1000);
        timeout.tv_usec = (long)((deadline - now) % 1000) * 1000;
        FD_ZERO(&readable);
        for (index = 0; index < socketCount; ++index)
        {
            FD_SET(sockets[index], &readable);
        }
        ready = select(0, &readable, NULL, NULL, &timeout);
        if (ready <= 0)
        {
            break;
        }
        for (index = 0; index < socketCount; ++index)
        {
            char response[2048];
            char location[LOCATION_CAPACITY];
            int  received;

            if (!FD_ISSET(sockets[index], &readable))
            {
                continue;
            }
            received = recvfrom(sockets[index], response, (int)(sizeof(response) - 1), 0, NULL, NULL);
            if (received <= 0)
            {
                continue;
            }
            response[received] = 0;
            if (0 != strncmp(response, "HTTP/1.1 200", 12) || !GetHeader(response, "LOCATION", location, sizeof(location)))
            {
                continue;
            }
            for (attempt = 0; attempt < locationCount; ++attempt)
            {
                if (0 == strcmp(locations[attempt], location))
                {
                    break;
                }
            }
            if (attempt == locationCount && locationCount < capacity)
            {
                memcpy(locations[locationCount++], location, sizeof(location));
            }
        }
    }
    for (index = 0; index < socketCount; ++index)
    {
        closesocket(sockets[index]);
    }
    for (index = 0; index < locationCount; ++index)
    {
        if (Describe(locations[index], &pDevices[deviceCount]))
        {
            ++deviceCount;
        }
    }
    return deviceCount;
}

int Ssdp_DescribeHost(const char* pHost, CastDevice* pDevice)
{
    char location[LOCATION_CAPACITY];

    (void)snprintf(location, sizeof(location), "http://%s:8008/ssdp/device-desc.xml", pHost);
    return Describe(location, pDevice);
}
