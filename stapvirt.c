// stapvirt - systemtap libvirt helper
// Copyright (C) 2013 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.
//

#include "config.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>

#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

int verbose = 0;

#define eprintf(format, args...) do {  \
    fprintf (stderr, format, ## args); } while (0)

#define dbug(verb, format, args...) do { \
    if (verb > verbose) break;           \
    eprintf("stapvirt:%s:%d " format,     \
        __FUNCTION__, __LINE__, ## args); } while (0)

#define err(format, args...) do { \
    dbug(2, "ERROR: " format, ## args); \
    eprintf("stapvirt: ERROR: " format, ## args); } while (0)

/* Converts a virDomainState into a human-friendly string */
static const char*
domainStateStr(virDomainState state)
{
    switch (state) {
    case VIR_DOMAIN_NOSTATE:
        return "no state";
    case VIR_DOMAIN_RUNNING:
        return "running";
    case VIR_DOMAIN_BLOCKED:
        return "blocked";
    case VIR_DOMAIN_PAUSED :
        return "paused";
    case VIR_DOMAIN_SHUTDOWN:
        return "shutting down";
    case VIR_DOMAIN_SHUTOFF:
        return "shutoff";
    case VIR_DOMAIN_CRASHED:
        return "crashed";
    case VIR_DOMAIN_PMSUSPENDED:
        return "suspended";
    default:
        break;
    }

    return "unknown state";
}

/* Used to silence libvirt error messages when necessary */
static void
silentErr(void __attribute__((__unused__)) *userdata, virErrorPtr err)
{
    dbug(2, "libvirt: code %d: domain %d: %s\n", err->code, err->domain,
                                                 err->message);
    fflush(stderr);
    return;
}

/* Tries to find a domain by name, ID, or UUID on the given node */
static virDomainPtr
findDomain(virConnectPtr conn, const char *domStr)
{
    virDomainPtr dom = NULL;

    // Override the default libvirt error handler so we silently fail lookups
    virSetErrorFunc(NULL, silentErr);

    // Try as an ID
    const char *end = domStr + strlen(domStr);
    char *stopchar = NULL;
    errno = 0;
    int id = strtoul(domStr, &stopchar, 0);
    if (errno == 0 && stopchar == end)
        dom = virDomainLookupByID(conn, id);

    // Try by name
    if (dom == NULL)
        dom = virDomainLookupByName(conn, domStr);

    // Try by UUID
    if (dom == NULL)
        dom = virDomainLookupByUUIDString(conn, domStr);

    virSetErrorFunc(NULL, NULL);
    return dom;
}

/* Retrieves the list of active domains using the old API. Returns the number
 * of domains collected in the ids array, or -1 on error. The array must be
 * freed if any domains are found. */
static int
getActiveDomains(virConnectPtr conn, int **ids)
{
    int idsn = 0;
    *ids = NULL;

    idsn = virConnectNumOfDomains(conn);
    if (idsn <= 0)
        return idsn;

    *ids = (int*) calloc(sizeof(int), idsn);
    if (*ids == NULL)
        return -1;

    idsn = virConnectListDomains(conn, *ids, idsn);
    if (idsn < 0) {
        free(*ids);
        *ids = NULL;
        return -1;
    }
    return idsn;
}

/* Retrieves the list of inactive domains using the old API. Returns the number
 * of domains collected in the names array, or -1 on error. The array must be
 * freed if any domains are found. */
static int
getInactiveDomains(virConnectPtr conn, char ***names)
{
    int namesn = 0;
    *names = NULL;

    namesn = virConnectNumOfDefinedDomains(conn);
    if (namesn <= 0)
        return namesn;

    *names = (char**) calloc(sizeof(char*), namesn);
    if (*names == NULL)
        return -1;

    namesn = virConnectListDefinedDomains(conn, *names, namesn);
    if (namesn < 0) {
        free(*names);
        *names = NULL;
        return -1;
    }
    return namesn;
}

/* Retrieves the list of all domains using the old API. Returns the number of
 * domains collected in the domains array, or -1 on error. The array must be
 * freed if any domains are found. */
static int
getAllDomains(virConnectPtr conn, virDomainPtr **domains)
{
    int *ids = NULL;
    char **names = NULL;
    int idsn, namesn, domainsn;
    idsn = namesn = domainsn = 0;
    *domains = NULL;

    // We need to use the older functions
    idsn = getActiveDomains(conn, &ids);
    if (idsn < 0) {
        err("Couldn't retrieve list of active domains\n");
        return -1;
    }
    namesn = getInactiveDomains(conn, &names);
    if (namesn < 0) {
        err("Couldn't retrieve list of inactive domains\n");
        domainsn = -1;
        goto cleanup_ids;
    }

    // Make sure we got something to print
    if (idsn + namesn == 0)
        goto cleanup_names;

    // Time to prepare virDomainPtr array
    *domains = (virDomainPtr*) calloc(sizeof(virDomainPtr), idsn+namesn);
    if (*domains == NULL) {
        err("Can't allocate domains array\n");
        goto cleanup_names;
    }

    int i;
    for (i = 0; i < idsn; i++) {
        virDomainPtr dom = virDomainLookupByID(conn, ids[i]);
        if (dom == NULL)
            continue;
        (*domains)[domainsn++] = dom;
    }
    for (i = 0; i < namesn; i++) {
        virDomainPtr dom = virDomainLookupByName(conn, names[i]);
        if (dom == NULL)
            continue;
        (*domains)[domainsn++] = dom;
    }

cleanup_names:
    free(names);
cleanup_ids:
    free(ids);
    return domainsn;
}

/* Returns nonzero if the libvirtd version is greater than target. Be careful
 * when using this function due to the double meaning of 0. */
static int
libvirtdCheckVers(virConnectPtr conn, unsigned long targetVer)
{
    unsigned long version;
    if (virConnectGetLibVersion(conn, &version) != 0) {
        err("Couldn't retrieve libvirtd version\n");
        return 0;
    }
    return (version >= targetVer);
}

/* Returns nonzero if libvirtd supports virDomainOpenChannel(). Be careful when
 * using this function due to the double meaning of 0. */
static int
libvirtdCanOpenChannel(virConnectPtr conn)
{
    // version = major * 1000000 + minor * 1000 + release
    // libvirt v1.0.2
    return libvirtdCheckVers(conn, 1*1000000+0*1000+2);
}

/* Returns nonzero if libvirtd supports virConnectListAllDomains(). Be careful
 * when using this function due to the double meaning of 0. */
static int
libvirtdCanListAll(virConnectPtr conn)
{
    // version = major * 1000000 + minor * 1000 + release
    // libvirt v0.9.13
    return libvirtdCheckVers(conn, 0*1000000+9*1000+13);
}

/* Returns nonzero if libvirtd supports hotplugging. Be careful when using this
 * function due to the double meaning of 0. */
static int
libvirtdCanHotplug(virConnectPtr conn)
{
    // version = major * 1000000 + minor * 1000 + release
    // libvirt v1.1.1
    return libvirtdCheckVers(conn, 1*1000000+1*1000+1);
}

/* Returns nonzero if libvirt supports hotplugging. Be careful when using this
 * function due to the double meaning of 0. */
static int
libvirtCanHotplug(void)
{
    unsigned long version;
    if (virGetVersion(&version, NULL, NULL) != 0) {
        err("Couldn't retrieve libvirt version\n");
        return 0;
    }

    // version = major * 1000000 + minor * 1000 + release
    // libvirt v1.1.1
    return (version >= (1*1000000+1*1000+1));
}

/* Returns nonzero if qemu supports hotplugging. Be careful when using this
 * function due to the double meaning of 0. */
static int
qemuCanHotplug(virConnectPtr conn)
{
    const char *hv = virConnectGetType(conn);
    if (hv == NULL) {
        err("Couldn't retrieve hypervisor type\n");
        return 0;
    }

    if (strcmp(hv, "QEMU") != 0)
        return 0;

    unsigned long version;
    if (virConnectGetVersion(conn, &version) != 0) {
        err("Couldn't retrieve hypervisor version\n");
        return 0;
    }

    // version = major * 1000000 + minor * 1000 + release
    // QEMU v0.10.0
    return (version >= (10*1000));
}

/* Returns nonzero if hotplugging is supported. Be careful when using this
 * function due to the double meaning of 0. */
static int
canHotplug(virConnectPtr conn)
{
    // We need to check if both the local library as well as the remote daemon
    // supports the holplugging API for virtio-serial
    return libvirtCanHotplug()
        && libvirtdCanHotplug(conn)
        && qemuCanHotplug(conn);
}

/* Retrieves and parses the domain's XML. Returns NULL on error or an xmlDocPtr
 * of the domain on success. Note that the xmlDocPtr returned must be freed by
 * xmlFreeDoc() */
static xmlDocPtr
getDomainXML(virDomainPtr dom, int liveConfig)
{
    virDomainXMLFlags flags = 0;
    if (!liveConfig)
        flags |= VIR_DOMAIN_XML_INACTIVE;

    char *domXML = virDomainGetXMLDesc(dom, flags);
    if (domXML == NULL) {
        err("Couldn't retrieve the domain's XML\n");
        return NULL;
    }

    xmlDocPtr domXMLDoc = xmlParseDoc(BAD_CAST domXML);
    if (domXMLDoc == NULL)
        err("Couldn't parse the domain's XML\n");

    free(domXML);
    return domXMLDoc;
}

/* Retrieves info from the port node. Used to retrieve either the path
 * attribute or the name attribute by getXmlPortPath() and getXmlPortName().
 * The advantage of this method over xmlGetProp() is that we don't have to
 * worry about de-allocating memory after. */
static const xmlChar*
getXmlPortInfo(xmlNodePtr port, const xmlChar* type, const xmlChar* attr)
{
    // find target type
    xmlNodePtr child = port->children;
    while (child != NULL && !xmlStrEqual(child->name, type))
        child = child->next;

    if (child == NULL) {
        err("Couldn't find target type\n");
        return NULL;
    }

    // find target attr
    xmlAttrPtr prop = child->properties;
    while (prop != NULL && !xmlStrEqual(prop->name, attr))
        prop = prop->next;

    if (prop == NULL || prop->children == NULL) {
        err("Couldn't find target attr\n");
        return NULL;
    }

    return prop->children->content;
}

static const char*
getXmlPortPath(xmlNodePtr port)
{
    return (const char *)
        getXmlPortInfo(port, BAD_CAST "source", BAD_CAST "path");
}

static const char*
getXmlPortName(xmlNodePtr port)
{
    return (const char *)
        getXmlPortInfo(port, BAD_CAST "target", BAD_CAST "name");
}

/* Returns nonzero if port is a valid SystemTap port */
static int
isValidPort(xmlNodePtr port, const char *basename)
{
    const char *name = getXmlPortName(port);
    if (name == NULL)
        return 0;

    // Check if it starts with basename
    if (strstr(name, basename) != name)
        return 0;

    // Check that the dot after basename is the last dot
    const char *lastdot = strrchr(name, '.');
    if (lastdot == NULL || (size_t)(lastdot-name) != strlen(basename))
        return 0;

    // Check that there is something between the dot and the end
    const char *end = name + strlen(name);
    if (lastdot >= end-1)
        return 0;

    // Check that the stuff between dot and end is a valid positive number
    errno = 0;
    char *stopchar = NULL;
    long num = strtol(lastdot+1, &stopchar, 10);
    if (errno || stopchar != end || num < 0)
        return 0;

    return 1;
}

/* Retrieves the list of SystemTap ports from a domain's XML doc. Note that the
 * xmlNodeSetPtr must be freed with xmlXPathFreeNodeSet() */
static xmlNodeSetPtr
getXmlPorts(xmlDocPtr domXMLDoc, const char *basename)
{
    xmlXPathContextPtr ctxt = xmlXPathNewContext(domXMLDoc);
    xmlXPathObjectPtr res = NULL;
    xmlNodeSetPtr ports = NULL;

    /* xpath expression is based on libguestfs code in src/libvirt-domain.c */

    // Unfortunately, the XPath of libxml2 does not support the matches()
    // function, which would allow us to use a regex to match the port name
    // precisely. Instead as a first approximation, we only require that the
    // port start with the basename. We then use isValidPort() to check for the
    // exact pattern.
    char *xpath;
    int rc = asprintf(&xpath, "//devices/channel[@type=\"unix\" and "
                        "./source/@mode=\"bind\" and "
                        "./source/@path and "
                        "./target/@type=\"virtio\" and "
                        "./target/@name[starts-with(.,\"%s\")]]", basename);
    if (rc == -1) {
        err("Couldn't call asprintf for xpath\n");
        goto cleanup;
    }

    res = xmlXPathEvalExpression(BAD_CAST xpath, ctxt);
    free(xpath);

    if (res == NULL) {
        err("Couldn't parse SystemTap ports\n");
        goto cleanup;
    }

    // We wait until now to initialize the return NodeSet since we know now that
    // the XPath eval went well
    ports = xmlXPathNodeSetCreate(NULL);

    // Go through each port and add only the valid ones to the NodeSet
    int i;
    for (i = 0; i < res->nodesetval->nodeNr; i++) {
        xmlNodePtr port = res->nodesetval->nodeTab[i];
        if (isValidPort(port, basename))
            xmlXPathNodeSetAdd(ports, port);
    }

    xmlXPathFreeObject(res);
cleanup:
    xmlXPathFreeContext(ctxt);
    return ports;
}

/* Retrieves the number of the port node. No error-checking is done since these
 * ports are assumed to have gone through isValidPort() */
static int
getXmlPortNumber(xmlNodePtr port)
{
    const char *name = getXmlPortName(port);
    char *lastdot = strrchr(name, '.');
    return strtol(lastdot+1, NULL, 10);
}

/* Shortcut to call getDomainXML and then getXmlPorts. Sets domXMLdoc and ports.
 * Returns zero on success and nonzero on error (in which case, no de-allocation
 * needs to be done by the caller). */
static int
getDomainPorts(virDomainPtr dom, int liveConfig, const char *basename,
                xmlDocPtr *domXMLdoc, xmlNodeSetPtr *ports)
{
    // We explicitly set domXMLdoc and ports to NULL in case of error so that
    // callers don't try to free them even if we failed

    *domXMLdoc = getDomainXML(dom, liveConfig);
    if (*domXMLdoc == NULL) {
        err("Couldn't get domain's XML\n");
        *ports = NULL;
        return -1;
    }

    *ports = getXmlPorts(*domXMLdoc, basename);
    if (*ports == NULL) {
        err("Couldn't search for SystemTap ports\n");
        xmlFreeDoc(*domXMLdoc);
        *domXMLdoc = NULL;
        return -1;
    }

    return 0;
}

/* Sets maxPort to the port in the NodeSet with the highest port number. Returns
 * zero on success, nonzero on error. */
static int
findPortWithMaxNum(xmlNodeSetPtr ports, xmlNodePtr *maxPort)
{
    // Go through each port and remember the highest port number
    // (libxml guarantees no particular order)
    *maxPort = NULL;
    int i, max = -1;
    for (i = 0; i < ports->nodeNr; i++) {
        xmlNodePtr port = ports->nodeTab[i];
        int num = getXmlPortNumber(port);
        if (num == -1)
            return -1; // proper error msg already emitted
        if (num > max) {
            max = num;
            *maxPort = port;
        }
    }

    // maxPort is either NULL (no ports in NodeSet) or has the port with max num
    return 0;
}

/* Creates an XML node for a SystemTap port with the given UNIX socket path and
 * name. */
static xmlNodePtr
createXmlPort(const char* path, const char* name)
{
    // No need for error-checking here, these functions cannot fail
    xmlNodePtr channel, node;

    channel = xmlNewNode(NULL, BAD_CAST "channel");
    xmlNewProp(channel, BAD_CAST "type", BAD_CAST "unix");
    node = xmlNewChild(channel, NULL, BAD_CAST "source", NULL);
    xmlNewProp(node, BAD_CAST "mode", BAD_CAST "bind");
    xmlNewProp(node, BAD_CAST "path", BAD_CAST path);
    node = xmlNewChild(channel, NULL, BAD_CAST "target", NULL);
    xmlNewProp(node, BAD_CAST "type", BAD_CAST "virtio");
    xmlNewProp(node, BAD_CAST "name", BAD_CAST name);

    return channel;
}

/* Looks at the list of ports and creates a new port with port number one higher
 * than the maximum of the existing port numbers. Returns zero on success,
 * nonzero on failure. */
static int
generateNextPort(xmlNodeSetPtr ports, virDomainPtr dom, const char *sockDir,
                 const char *basename, xmlNodePtr *newPort)
{
    int ret = 0;
    xmlNodePtr maxPort;
    if (findPortWithMaxNum(ports, &maxPort) != 0)
        return -1; // proper error msg already emitted

    // maxPort == NULL means this will be the first port
    int next = (maxPort == NULL) ? 0 : (getXmlPortNumber(maxPort)+1);

    const char *domName = virDomainGetName(dom);
    if (domName == NULL) {
        err("Couldn't retrieve domain name\n");
        return -1;
    }

    char *path;
    if (asprintf(&path, "%s/%s.%s.%d.sock",
                 sockDir, domName, basename, next) == -1) {
        err("Couldn't call asprintf for path\n");
        return -1;
    }

    char *name;
    if (asprintf(&name, "%s.%d", basename, next) == -1) {
        err("Couldn't call asprintf for name\n");
        ret = -1;
        goto cleanup;
    }

    *newPort = createXmlPort(path, name);
    free(name);

cleanup:
    free(path);
    return ret;
}

/* Adds an XML node representing a SystemTap port to the <devices> list. Returns
 * zero on success, nonzero on failure. */
static int
addXmlPort(xmlDocPtr domXMLDoc, xmlNodePtr port)
{
    int ret = 0;

    // Find channel node
    xmlXPathContextPtr ctxt = xmlXPathNewContext(domXMLDoc);
    xmlXPathObjectPtr devres =
        xmlXPathEvalExpression(BAD_CAST "//devices", ctxt);
    if (devres == NULL) {
        err("Couldn't search for <devices> node\n");
        ret = -1;
        goto cleanup_ctxt;
    }

    if (xmlXPathNodeSetIsEmpty(devres->nodesetval)) {
        err("No <devices> node found in domain XML\n");
        ret = -1;
        goto cleanup_devres;
    }

    xmlNodePtr devices = devres->nodesetval->nodeTab[0];
    if (xmlAddChild(devices, port) == NULL) {
        err("Could not add port in XML\n");
        ret = -1;
        goto cleanup_devres;
    }

cleanup_devres:
    xmlXPathFreeObject(devres);
cleanup_ctxt:
    xmlXPathFreeContext(ctxt);
    return ret;
}

/* Redefines the domain defined by domXMLdoc at the hypervisor connected to
 * conn. Returns zero on success and nonzero on failure. */
static int
redefineDomain(virConnectPtr conn, xmlDocPtr domXMLdoc)
{
    xmlChar *buf;
    xmlDocDumpMemoryEnc(domXMLdoc, &buf, NULL, "UTF-8");

    virDomainPtr rcDom = virDomainDefineXML(conn, (const char*) buf);
    xmlFree(buf);

    if (rcDom == NULL) {
        err("Couldn't redefine the domain\n");
        return -1;
    }
    virDomainFree(rcDom);

    return 0;
}

/* Attaches or detaches a port to a live domain. Returns zero on success and
 * nonzero on failure. */
static int
hotplugPort(int attach, virDomainPtr dom,
               xmlDocPtr domXMLdoc, xmlNodePtr port)
{
    int ret = 0;
    xmlBufferPtr buf = xmlBufferCreate();

    if (xmlNodeDump(buf, domXMLdoc, port, 0, 0) == -1) {
        err("Couldn't dump new port XML\n");
        ret = -1;
        goto cleanup_buf;
    }

    int rc = attach ? virDomainAttachDevice(dom, (const char *) buf->content)
                    : virDomainDetachDevice(dom, (const char *) buf->content);
    if (rc != 0) {
        err("Couldn't %s the device\n", attach ? "attach" : "detach");
        ret = -1;
        goto cleanup_buf;
    }

cleanup_buf:
    xmlBufferFree(buf);
    return ret;
}

/* Tries to open one of the ports in the set on the target domain. Returns NULL
 * if an error occurred. Note that the virStreamPtr object must be freed with
 * virStreamFree(). */
static virStreamPtr
domainOpenChannel(virConnectPtr conn, virDomainPtr dom, xmlNodeSetPtr ports)
{
    virStreamPtr stream = virStreamNew(conn, VIR_STREAM_NONBLOCK);
    if (stream == NULL) {
        err("Couldn't create a new stream object\n");
        return NULL;
    }

    // Override libvirt default err handler so that we suppress error msgs when
    // we try to connect to busy channels
    virSetErrorFunc(NULL, silentErr);

    int i;
    for (i = 0; i < ports->nodeNr; i++) {
        xmlNodePtr port = ports->nodeTab[i];
        const char *name = getXmlPortName(port);
        if (name == NULL)
            continue;
        if (virDomainOpenChannel(dom, name, stream, 0) == 0)
            break;
        dbug(1, "channel %s is already in use\n", name);
    }

    // Restore error handler to the default
    virSetErrorFunc(NULL, NULL);

    if (i == ports->nodeNr) {
        virStreamFree(stream);
        return NULL;
    }

    return stream;
}

static void
printName(virDomainPtr dom)
{
    const char *name = virDomainGetName(dom);
    printf("%s", name != NULL ? name : "<undefined>");
}

static void
printUUID(virDomainPtr dom)
{
    char uuid[VIR_UUID_STRING_BUFLEN];
    if (virDomainGetUUIDString(dom, (char *) &uuid) != 0)
        strncpy(uuid, "INVALID_UUID", VIR_UUID_STRING_BUFLEN);
    printf("%s", uuid);
}

static void
printState(virDomainPtr dom)
{
    int state;
    if (virDomainGetState(dom, &state, NULL, 0) != 0)
        state = VIR_DOMAIN_NOSTATE;
    printf("%s", domainStateStr(state));
}

static void
printID(virDomainPtr dom)
{
    if (virDomainIsActive(dom) == 1)
        printf("%u", virDomainGetID(dom));
    else
        printf("-");
}

static void
printType(virDomainPtr dom)
{
    int pers = virDomainIsPersistent(dom);
    if (pers == -1)
        printf("<undefined>");
    else
        printf(pers ? "persistent" : "transient");
}

static void
printHotplug(virConnectPtr conn)
{
    if (!canHotplug(conn))
        printf("not ");
    printf("supported");
}

static void
printPortNum(virDomainPtr dom, const char *basename)
{
    xmlDocPtr domXMLdoc = NULL;
    xmlNodeSetPtr ports = NULL;

    if (getDomainPorts(dom, 0, basename, &domXMLdoc, &ports) != 0)
        return; // proper error msg already emitted

    printf("%u", ports->nodeNr);
    xmlXPathFreeNodeSet(ports);
    xmlFreeDoc(domXMLdoc);
}

/**************************************
 *********** USER COMMANDS ************
 **************************************
 */

#define PORT_BASENAME "org.systemtap.stapsh"

static int cmd_list               (void);
static int cmd_port_add           (void);
static int cmd_port_remove        (void);
static int cmd_query              (void);
static int cmd_port_list          (void);
static int cmd_connect            (void);

typedef struct {
    const char *name;
    int need_domain;
    int read_only;
    int (*fn)(void);
} command;

const command commands[] = {
      { "list",         0, 1, cmd_list },
      { "port-add",     1, 0, cmd_port_add },
      { "port-remove",  1, 0, cmd_port_remove },
      { "query",        1, 1, cmd_query },
      { "port-list",    1, 1, cmd_port_list },
      { "connect",      1, 0, cmd_connect },
};
const unsigned ncommands = sizeof(commands) / sizeof(*commands);

char *sockDir = "/var/lib/libvirt/qemu";
char *virURI = NULL;
virConnectPtr conn = NULL;

char *targetDomStr = NULL;
virDomainPtr targetDom = NULL;

static int
cmd_list()
{
    int domainsn = 0;
    virDomainPtr *domains = NULL;

    printf("Available domains on URI '%s':\n", virConnectGetURI(conn));
    printf("ID\tState\tType\tName\n");

    // virConnectListAllDomains is more safe, but was introduced in 0.9.13
    domainsn = libvirtdCanListAll(conn)
                ? virConnectListAllDomains(conn, &domains, 0)
                : getAllDomains(conn, &domains);
    if (domainsn < 0) {
        err("Couldn't retrieve list of domains\n");
        return -1;
    }

    int i;
    for (i = 0; i < domainsn; i++) {
        printID(domains[i]);    printf("\t");
        printState(domains[i]); printf("\t");
        printType(domains[i]);  printf("\t");
        printName(domains[i]);  printf("\n");
        virDomainFree(domains[i]);
    }

    free(domains);
    return 0;
}

static int
cmd_query()
{
    // start with newlines because the virPrint* funcs don't put them
    printf("\n              Name:  "); printName(targetDom);
    printf("\n              UUID:  "); printUUID(targetDom);
    printf("\n             State:  "); printState(targetDom);
    printf("\n                ID:  "); printID(targetDom);
    printf("\n              Type:  "); printType(targetDom);
    printf("\n   Permanent Ports:  "); printPortNum(targetDom,PORT_BASENAME);
    printf("\n       Hotplugging:  "); printHotplug(conn);
    printf("\n\n");

    return 0;
}

static int
cmd_port_add()
{
    int rc = 0;
    xmlDocPtr domXMLdoc = NULL;
    xmlNodeSetPtr ports = NULL;
    xmlNodePtr newPort = NULL;

    if (getDomainPorts(targetDom, 0, PORT_BASENAME, &domXMLdoc, &ports) != 0)
        return -1;  // proper error msg already emitted
    if (generateNextPort(ports, targetDom, sockDir,
                         PORT_BASENAME, &newPort) != 0)
        goto error; // proper error msg already emitted
    if (addXmlPort(domXMLdoc, newPort) != 0)
        goto error; // proper error msg already emitted
    if (redefineDomain(conn, domXMLdoc) != 0)
        goto error; // proper error msg already emitted

    printf("Added new port %s\n", getXmlPortName(newPort));
    if (virDomainIsActive(targetDom) == 1) // helpful hint for user
        printf("The domain must be restarted before changes take effect.\n");

    goto cleanup;
error:
    rc = -1;
cleanup:
    xmlXPathFreeNodeSet(ports);
    xmlFreeDoc(domXMLdoc);
    return rc;
}

static int
cmd_port_remove()
{
    int rc = 0;
    xmlDocPtr domXMLdoc = NULL;
    xmlNodeSetPtr ports = NULL;
    xmlNodePtr maxPort = NULL;

    if (getDomainPorts(targetDom, 0, PORT_BASENAME, &domXMLdoc, &ports) != 0)
        return -1;  // proper error msg already emitted
    if (findPortWithMaxNum(ports, &maxPort) != 0)
        goto error; // proper error msg already emitted

    if (maxPort == NULL) {
        err("No SystemTap port to remove\n");
        goto error;
    }

    xmlUnlinkNode(maxPort);
    if (redefineDomain(conn, domXMLdoc) != 0)
        goto error; // proper error msg already emitted

    printf("Removed port %s\n", getXmlPortName(maxPort));
    if (virDomainIsActive(targetDom) == 1) // helpful hint for user
        printf("The domain must be restarted before changes take effect.\n");

    goto cleanup;
error:
    rc = -1;
cleanup:
    xmlXPathFreeNodeSet(ports);
    xmlFreeDoc(domXMLdoc);
    return rc;
}

static int
cmd_port_list()
{
    xmlDocPtr domXMLdoc = NULL;
    xmlNodeSetPtr ports = NULL;

    if (getDomainPorts(targetDom, 0, PORT_BASENAME, &domXMLdoc, &ports) != 0)
        return -1; // proper error msg already emitted

    int i;
    for (i = 0; i < ports->nodeNr; i++) {
        xmlNodePtr port = ports->nodeTab[i];
        printf("%s\n", getXmlPortPath(port));
    }

    xmlXPathFreeNodeSet(ports);
    xmlFreeDoc(domXMLdoc);
    return 0;
}

// The structure of the code for cmd_connect and the event callbacks is largely
// based on the "virsh console" command

int disconnect = 0;

typedef struct {
    virStreamPtr st;
    int    stdin_w;
    int    stdout_w;
    char   termbuf[1024]; /* term to st */
    size_t termbuf_off;
    char   stbuf[1024]; /* st to term */
    size_t stbuf_off;
} event_context;

static void
stdin_event(__attribute__((unused)) int watch, int fd,
                                    int events, void *opaque)
{
    event_context *ctxt = opaque;

    if ((events & VIR_EVENT_HANDLE_READABLE)
            && (ctxt->termbuf_off < sizeof(ctxt->termbuf))) {
        // if there's no space in the buffer, we need to wait until more has
        // been written to the stream

        int bytes_read = read(fd, ctxt->termbuf + ctxt->termbuf_off,
                              sizeof(ctxt->termbuf) - ctxt->termbuf_off);
        if (bytes_read < 0) {
            if (errno != EAGAIN)
                disconnect = 1;
            return;
        }
        if (bytes_read == 0) {
            disconnect = 1;
            virStreamFinish(ctxt->st);
            return;
        }

        ctxt->termbuf_off += bytes_read;
    }

    if (ctxt->termbuf_off) { // we have stuff to write to the stream
        virStreamEventUpdateCallback(ctxt->st, VIR_STREAM_EVENT_READABLE
                                             | VIR_STREAM_EVENT_WRITABLE);
    }

    if (events & (VIR_EVENT_HANDLE_ERROR|VIR_EVENT_HANDLE_HANGUP))
        disconnect = 1;
}

static void
stdout_event(__attribute__((unused)) int watch, int fd,
                                     int events, void *opaque)
{
    event_context *ctxt = opaque;

    if (events & VIR_EVENT_HANDLE_WRITABLE && ctxt->stbuf_off) {
        ssize_t bytes_written = write(fd, ctxt->stbuf, ctxt->stbuf_off);
        if (bytes_written < 0) {
            if (errno != EAGAIN)
                disconnect = 1;
            return;
        }
        // shift down
        memmove(ctxt->stbuf, ctxt->stbuf + bytes_written,
                             ctxt->stbuf_off - bytes_written);
        ctxt->stbuf_off -= bytes_written;
    }

    if (ctxt->stbuf_off == 0) // there's nothing else to write to stdout
        virEventUpdateHandle(ctxt->stdout_w, 0);

    if (events & (VIR_EVENT_HANDLE_ERROR|VIR_EVENT_HANDLE_HANGUP))
        disconnect = 1;
}

static void
stream_event(virStreamPtr st, int events, void *opaque)
{
    event_context *ctxt = opaque;

    if ((events & VIR_STREAM_EVENT_READABLE)
            && (ctxt->stbuf_off < sizeof(ctxt->stbuf))) {
        // if there's no space in the buffer, we need to wait until more has
        // been written to stdout

        int bytes_recv = virStreamRecv(st, ctxt->stbuf + ctxt->stbuf_off,
                                       sizeof(ctxt->stbuf) - ctxt->stbuf_off);
        if (bytes_recv == -2)
            return; // EAGAIN
        if (bytes_recv <= 0) {
            if (bytes_recv == 0)
                virStreamFinish(st);
            disconnect = 1;
            return;
        }
        ctxt->stbuf_off += bytes_recv;
    }

    if (ctxt->stbuf_off) // we have stuff to write to stdout
        virEventUpdateHandle(ctxt->stdout_w, VIR_EVENT_HANDLE_WRITABLE);

    if (events & VIR_STREAM_EVENT_WRITABLE && ctxt->termbuf_off) {
        ssize_t bytes_sent = virStreamSend(st, ctxt->termbuf,
                                               ctxt->termbuf_off);
        if (bytes_sent == -2)
            return;
        if (bytes_sent < 0) {
            disconnect = 1;
            return;
        }
        // shift down
        memmove(ctxt->termbuf, ctxt->termbuf + bytes_sent,
                               ctxt->termbuf_off - bytes_sent);
        ctxt->termbuf_off -= bytes_sent;
    }

    if (!ctxt->termbuf_off) // there's nothing else to write to the stream
        virStreamEventUpdateCallback(st, VIR_STREAM_EVENT_READABLE);

    if (events & (VIR_STREAM_EVENT_ERROR|VIR_STREAM_EVENT_HANGUP))
        disconnect = 1;
}

static void
on_child_exit(int sig)
{
    if (sig == SIGCHLD) // sanity check
        disconnect = 1;
}

static int
cmd_connect()
{
    int rc = 0;
    xmlDocPtr domXMLdoc = NULL;
    xmlNodeSetPtr ports = NULL;
    xmlNodePtr newPort = NULL;
    int hotplugged = 0;

    event_context ctxt;
    memset(&ctxt, 0, sizeof(ctxt));

    // check that libvirtd supports channels
    if (!libvirtdCanOpenChannel(conn)) {
        err("Channel support requires libvirtd v1.0.2+\n");
        return -1;
    }

    if (virDomainIsActive(targetDom) != 1) {
        err("The domain is not running\n");
        return -1;
    }

    // NB: setting liveConfig to 1
    if (getDomainPorts(targetDom, 1, PORT_BASENAME, &domXMLdoc, &ports) != 0)
        return -1; // proper error msg already emitted

    // give a nice hint to the user
    if (ports->nodeNr == 0 && !canHotplug(conn)) {
        err("No SystemTap ports detected and hotplugging not available. Try "
            "using the command \'stapvirt port-add %s\' to add a port.\n",
            virDomainGetName(targetDom));
        goto error;
    }

    // print an error if no channel found and we can't hotplug
    dbug(2, "trying to open a permanent port\n");
    ctxt.st = domainOpenChannel(conn, targetDom, ports);
    if (ctxt.st == NULL && !canHotplug(conn)) {
        err("Couldn't connect to any SystemTap port. Try using the command "
            "\'stapvirt port-add %s\' if more SystemTap ports are required.\n",
            virDomainGetName(targetDom));
        goto error;
    }

    // let's try hotplugging!
    if (ctxt.st == NULL) {
        dbug(2, "trying to hotplug a port\n");
        if (generateNextPort(ports, targetDom, sockDir,
                             PORT_BASENAME, &newPort) != 0)
            goto error; // proper error msg already emitted
        if (hotplugPort(1, targetDom, domXMLdoc, newPort) != 0)
            goto error; // proper error msg already emitted
        hotplugged = 1;

        ctxt.st = virStreamNew(conn, VIR_STREAM_NONBLOCK);
        if (ctxt.st == NULL) {
            err("Couldn't create a new stream object\n");
            goto error;
        }

        const char *name = getXmlPortName(newPort);
        if (virDomainOpenChannel(targetDom, name, ctxt.st, 0) != 0) {
            err("Couldn't open channel on hotplugged port\n");
            goto error_st;
        }
        dbug(2, "successfully hotplugged a port\n");
    } else {
        dbug(2, "successfully opened permanent port\n");
    }

    // change stdin and stdout to O_NONBLOCK: even if libvirt uses poll(), it
    // can happen for read/write to block even though we got POLLIN/POLLOUT
    // (see the BUGS section of select(2))
    int flags = fcntl(STDIN_FILENO, F_GETFL);
    if (flags == -1 || fcntl(STDIN_FILENO, F_SETFL, flags|O_NONBLOCK) == -1) {
        err("Couldn't change stdin to non-blocking mode\n");
        goto error_st;
    }
    flags = fcntl(STDOUT_FILENO, F_GETFL);
    if (flags == -1 || fcntl(STDOUT_FILENO, F_SETFL, flags|O_NONBLOCK) == -1) {
        err("Couldn't change stdout to non-blocking mode\n");
        goto error_st;
    }

    // install callbacks
    ctxt.stdin_w = virEventAddHandle(STDIN_FILENO, VIR_EVENT_HANDLE_READABLE,
                                    stdin_event, &ctxt, NULL);
    if (ctxt.stdin_w < 0) {
        err("Couldn't add handle for stdin\n");
        goto error_st;
    }
    ctxt.stdout_w = virEventAddHandle(STDOUT_FILENO, 0,
                                      stdout_event, &ctxt, NULL);
    if (ctxt.stdout_w < 0) {
        err("Couldn't add handle for stdout\n");
        goto error_st;
    }
    if (virStreamEventAddCallback(ctxt.st, VIR_STREAM_EVENT_READABLE,
                                  stream_event, &ctxt, NULL) < 0) {
        err("Couldn't add handle for stream\n");
        goto error_st;
    }

    // install signal handler for SIGCHLD so that we stop the connection in case
    // the underlying transport app (e.g. ssh) exits (see also related note in
    // the libvirt_stapsh class in remote.cxx)
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_child_exit;
    if (sigaction(SIGCHLD, &sa, NULL) != 0) {
        err("Couldn't add signal handler for SIGCHLD\n");
        goto error_st;
    }

    // silence libvirt as some error messages might occur when child processes
    // such as ssh receive SIGINT
    virSetErrorFunc(NULL, silentErr);

    dbug(2, "entering event loop\n");
    while (!disconnect) {
        if (virEventRunDefaultImpl() != 0)
            break;
    }

    virStreamAbort(ctxt.st);
    virStreamFree(ctxt.st);

    // restore error printing
    virSetErrorFunc(NULL, NULL);

    goto cleanup;

error_st:
    virStreamFree(ctxt.st);
error:
    rc = -1;
cleanup:
    if (hotplugged && hotplugPort(0, targetDom, domXMLdoc, newPort) != 0)
        dbug(2, "could not unplug port\n");
    xmlXPathFreeNodeSet(ports);
    xmlFreeDoc(domXMLdoc);
    return rc;
}

static void
usage(int code)
{
    // 80 screen width = 94 here
    // The port-add command purposely does not also hotplug the port, even if
    // supported
    //   - It would make it harder to figure out the final state
    //   - If hotplugging is supported, users would not need to do port-add in
    //     the first place, since they would let stap do the hotplugging
    eprintf("stapvirt v%s\n", VERSION);
    eprintf("Usage: stapvirt [-c URI] [-d PATH] [-v] COMMAND ARGUMENTS\n\n");
    eprintf("     -c    Specify the libvirt driver URI to which to connect [default: NULL]\n");
    eprintf("     -d    Specify the directory in which sockets should be placed [default:\n");
    eprintf("           /var/lib/libvirt/qemu]\n");
    eprintf("     -v    Increase verbosity\n");
    eprintf("\n");

    if (code != 0) {
        eprintf("Try the 'help' command for more information.\n");
        exit(code);
    }

    eprintf("Available commands are:\n");
    eprintf("\n");
    eprintf("     help\n");
    eprintf("           Display this message.\n");
    eprintf("     list\n");
    eprintf("           List available domains.\n");
    eprintf("     port-add <domain>\n");
    eprintf("           Add a permanent SystemTap port to the domain's definition. If the\n");
    eprintf("           domain is currently running, it must be restarted before changes take\n");
    eprintf("           effect.\n");
    eprintf("     port-list <domain>\n");
    eprintf("           List the UNIX socket paths of the permanent SystemTap ports in the\n");
    eprintf("           domain's definition.\n");
    eprintf("     port-remove <domain>\n");
    eprintf("           Remove a permanent SystemTap port from the domain's definition. If\n");
    eprintf("           the domain is currently running, it must be restarted before changes\n");
    eprintf("           take effect.\n");
    eprintf("     query <domain>\n");
    eprintf("           Display the following information about the domain: its name, its\n");
    eprintf("           UUID, its state, the number of permanent SystemTap ports installed,\n");
    eprintf("           and whether hotplugging is supported.\n");
    /* UNDOCUMENTED COMMAND (for stap only)
    eprintf("     connect <domain>\n");
    eprintf("           Open up a connection to the domain. If successful, stapvirt becomes a\n");
    eprintf("           pass-through for stapsh. If no port found and hotplugging is\n");
    eprintf("           supported, then a new port is hotplugged for the duration of the\n");
    eprintf("           connection.\n");
    */
    eprintf("\n");
    eprintf("Domains can be specified using their name, UUID, or ID.\n");
    eprintf("\n");
    exit(code);
}

static void
parse_args(int argc, char* const argv[])
{
    int c;
    while ((c = getopt(argc, argv, "c:d:hv")) != -1) {
        dbug(2, "received opt %c\n", c);
        switch (c) {
        case 'c':
            virURI = optarg;
            break;
        case 'd':
            sockDir = optarg;
            break;
        case 'h':
            usage(0); // no return
        case 'v':
            verbose++;
            break;
        default:
            usage(1); // no return
        }
    }
}

static const command*
lookupCommand(const char *cmd)
{
    unsigned i;
    for (i = 0; i < ncommands; ++i) {
        if (strcmp(cmd, commands[i].name) == 0)
            return &commands[i];
    }
    return NULL;
}

static int
preCommand(const command *cmd)
{
    // virConnect() may not be our first call, so let's explicitly initialize it
    // unconditionally
    if (virInitialize() != 0) {
        err("Couldn't initialize libvirt library\n");
        return -1;
    }

    // For connect to work, we need to call virEventRegisterDefaultImpl()
    // before even connecting. This requirement may be because we don't
    // use threads
    if (strcmp(cmd->name, "connect") == 0) {
        if (virEventRegisterDefaultImpl() != 0) {
            err("Couldn't register the default event implementation\n");
            return -1;
        }
    }

    dbug(1, "opening%s connection on URI '%s'\n",
        cmd->read_only ? " read-only" : "", virURI);

    conn = virConnectOpenAuth(virURI, virConnectAuthPtrDefault,
                              cmd->read_only ? VIR_CONNECT_RO : 0);
    if (conn == NULL) {
        err("Couldn't connect to %s\n", virURI);
        return -1;
    }

    // if the command needs it, retrieve the domain
    if (cmd->need_domain && targetDomStr != NULL) {
        dbug(2, "searching for domain '%s'\n", targetDomStr);
        targetDom = findDomain(conn, targetDomStr);
        if (targetDom == NULL) {
            err("Couldn't find domain '%s' on URI '%s'\n",
                targetDomStr, virConnectGetURI(conn));
            virConnectClose(conn);
            return -1;
        }
    }

    return 0;
}

static void
postCommand(void)
{
    if (targetDom != NULL)
        virDomainFree(targetDom);
    virConnectClose(conn);
}

int
main(int argc, char* const argv[])
{
    // initialize lib and make sure there's no version mismatch
    LIBXML_TEST_VERSION

    parse_args(argc, argv);

    // move to first non-option argument
    // getopt() puts them all at the end for us
    if (optind > 0) {
        argc -= optind;
        argv += optind;
    }

    // check that we have at least a command
    if (argc <= 0)
        usage(1); // no return
    else if (strcmp(argv[0], "help") == 0)
        usage(0); // no return

    dbug(2, "looking up command '%s'\n", argv[0]);
    const command *cmd = lookupCommand(argv[0]);
    if (cmd == NULL) {
        err("Invalid command '%s'\n", argv[0]);
        usage(1); // no return
    }

    if (cmd->need_domain) {
        if (argc < 2) {
            err("Command %s requires a <domain> argument\n", cmd->name);
            usage(1); // no return
        } else {
            targetDomStr = argv[1];
        }
    }

    if (preCommand(cmd) == -1)
        return EXIT_FAILURE; // preCommand printed an error already

    dbug(2, "calling command %s\n", cmd->name);
    int rc = cmd->fn();
    postCommand();

    return rc;
}

/* vim: set sw=4 : */
