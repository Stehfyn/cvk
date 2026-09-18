/* ssdp.h - finds Cast receivers on the local network. Chromecast built-in devices answer an
** SSDP M-SEARCH for the DIAL service and publish their name in the DIAL device description
** on port 8008; the Cast channel itself is always port 8009 on the same host. */
#ifndef CAST_SSDP_H
#define CAST_SSDP_H

#define CAST_MAX_DEVICES (16)

typedef struct CastDevice
{
    char friendlyName[128];
    char model[64];
    char host[64]; /* dotted IPv4 address */
} CastDevice;

/* Searches every IPv4 interface for timeoutMs and describes each responder.
** WSAStartup must have been called. Returns the number of devices written. */
unsigned Ssdp_DiscoverCastDevices(CastDevice* pDevices, unsigned capacity, unsigned timeoutMs);

/* Reads the DIAL description of one known host. Returns 1 when a name was found. */
int Ssdp_DescribeHost(const char* pHost, CastDevice* pDevice);

#endif
