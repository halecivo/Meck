#pragma once
// =============================================================================
// CalDAV/CardDAV client (docs/caldav-design.md) -- the parts without hardware
//
// URL handling, HTTP Basic, a small XML reader for the multistatus
// replies of PROPFIND and REPORT, and the vCard reading for CardDAV contacts. The replies are flat enough that a tag
// scanner does instead of an XML library: namespace prefixes are dropped,
// entities and CDATA are decoded, and one DavResponse is handed to the
// callback per <response>. No Arduino code here, so test/dav/test_dav.cpp
// runs it on a PC.
// =============================================================================

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <strings.h>

// --- URLs ----------------------------------------------------------------------

static inline bool davUrlIsHttps(const char* url) { return url && strncmp(url, "https://", 8) == 0; }

// "https://host:port" of a URL, no path. False when it is not an http(s) URL.
static inline bool davUrlOrigin(const char* url, char* out, size_t n) {
  if (!url || !out || n == 0) return false;
  out[0] = 0;
  size_t sl = strncmp(url, "https://", 8) == 0 ? 8 : strncmp(url, "http://", 7) == 0 ? 7 : 0;
  if (!sl) return false;
  size_t hl = strcspn(url + sl, "/?#");
  if (hl == 0 || sl + hl + 1 > n) return false;
  memcpy(out, url, sl + hl);
  out[sl + hl] = 0;
  return true;
}

// Host name without port: "https://dav.example.com:8443/x" -> "dav.example.com"
static inline void davUrlHost(const char* url, char* out, size_t n) {
  if (n == 0) return;
  out[0] = 0;
  const char* p = strstr(url ? url : "", "://");
  if (!p) return;
  p += 3;
  size_t l = strcspn(p, "/:?#");
  if (l >= n) l = n - 1;
  memcpy(out, p, l);
  out[l] = 0;
}

// What the user typed into the form, as a server URL: spaces trimmed, https
// when no scheme is given, the path kept (Nextcloud lives under /remote.php/dav),
// no query or fragment, no trailing slash. False for anything that is not a
// plausible http(s) address (user:password@ in it included: those go in the
// fields).
static inline bool davNormalizeUrl(const char* in, char* out, size_t n) {
  if (!in || !out || n < 12) return false;
  while (*in == ' ' || *in == '\t') in++;
  char tmp[200];
  size_t l = strlen(in);
  while (l && (in[l - 1] == ' ' || in[l - 1] == '\t' || in[l - 1] == '\r' || in[l - 1] == '\n')) l--;
  if (l == 0 || l >= sizeof(tmp)) return false;
  memcpy(tmp, in, l);
  tmp[l] = 0;
  const char* scheme = "https://";
  const char* rest = tmp;
  if (strncmp(tmp, "http://", 7) == 0) { scheme = "http://"; rest = tmp + 7; }
  else if (strncmp(tmp, "https://", 8) == 0) { rest = tmp + 8; }
  else if (strstr(tmp, "://")) return false;
  size_t hl = strcspn(rest, "/?#");
  if (hl == 0) return false;
  for (size_t i = 0; i < hl; i++) {
    unsigned char ch = (unsigned char)rest[i];
    if (ch <= ' ' || ch == '@' || ch == '\\') return false;
  }
  size_t pl = strcspn(rest + hl, "?#");
  while (pl > 0 && rest[hl + pl - 1] == '/') pl--;
  for (size_t i = 0; i < pl; i++)
    if ((unsigned char)rest[hl + i] <= ' ') return false;
  if (strlen(scheme) + hl + pl + 1 > n) return false;
  snprintf(out, n, "%s%.*s%.*s", scheme, (int)hl, rest, (int)pl, rest + hl);
  return true;
}

// A href from a multistatus reply against the URL it was asked from: absolute
// URL as it is, "/path" on the same origin, anything else next to the base.
static inline bool davResolve(const char* base, const char* href, char* out, size_t n) {
  if (!base || !href || !out || n == 0) return false;
  out[0] = 0;
  if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0) {
    if (strlen(href) + 1 > n) return false;
    snprintf(out, n, "%s", href);
    return true;
  }
  char origin[128];
  if (!davUrlOrigin(base, origin, sizeof(origin))) return false;
  if (href[0] == '/') {
    if (strlen(origin) + strlen(href) + 1 > n) return false;
    snprintf(out, n, "%s%s", origin, href);
    return true;
  }
  const char* slash = strrchr(base + strlen(origin), '/');
  size_t dir = slash ? (size_t)(slash - base) + 1 : strlen(origin);
  if (dir + strlen(href) + (slash ? 1 : 2) > n) return false;
  snprintf(out, n, "%.*s%s%s", (int)dir, base, slash ? "" : "/", href);
  return true;
}

// Path part of a URL ("/" when there is none): what a hostile server cannot
// use to send the deck to another host once the origin has been checked.
static inline const char* davUrlPath(const char* url) {
  char origin[128];
  if (!davUrlOrigin(url, origin, sizeof(origin))) return "/";
  const char* p = url + strlen(origin);
  return *p ? p : "/";
}

// Same scheme, host and port: credentials only ever go to the origin the user
// typed in, also when a reply names another href
static inline bool davSameOrigin(const char* a, const char* b) {
  char oa[128], ob[128];
  if (!davUrlOrigin(a, oa, sizeof(oa)) || !davUrlOrigin(b, ob, sizeof(ob))) return false;
  return strcasecmp(oa, ob) == 0;
}

// Credentials go to the origin the user typed in, and to nothing else, except
// for the providers that answer from a partition host of their own (iCloud:
// caldav.icloud.com sends the client to pNN-caldav.icloud.com). A short list
// of such domains; a public-suffix heuristic would let a hostile server on
// the same suffix (co.uk) collect the password.
static inline bool davHostInDomain(const char* host, const char* domain) {
  size_t hl = strlen(host), dl = strlen(domain);
  if (hl < dl) return false;
  if (strcasecmp(host + hl - dl, domain) != 0) return false;
  return hl == dl || host[hl - dl - 1] == '.';
}

static inline bool davSameSite(const char* a, const char* b) {
  if (davSameOrigin(a, b)) return true;
  if (!davUrlIsHttps(a) || !davUrlIsHttps(b)) return false;
  char ha[96], hb[96];
  davUrlHost(a, ha, sizeof(ha));
  davUrlHost(b, hb, sizeof(hb));
  return davHostInDomain(ha, "icloud.com") && davHostInDomain(hb, "icloud.com");
}

// --- Basic authentication -----------------------------------------------------------

static inline size_t davBase64(char* out, size_t n, const uint8_t* in, size_t len) {
  static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t need = (len + 2) / 3 * 4 + 1;
  if (!out || n < need) { if (out && n) out[0] = 0; return 0; }
  size_t o = 0;
  for (size_t i = 0; i < len; i += 3) {
    uint32_t v = (uint32_t)in[i] << 16;
    if (i + 1 < len) v |= (uint32_t)in[i + 1] << 8;
    if (i + 2 < len) v |= in[i + 2];
    out[o++] = tbl[(v >> 18) & 63];
    out[o++] = tbl[(v >> 12) & 63];
    out[o++] = i + 1 < len ? tbl[(v >> 6) & 63] : '=';
    out[o++] = i + 2 < len ? tbl[v & 63] : '=';
  }
  out[o] = 0;
  return o;
}

// "Basic dXNlcjpwYXNz" for the Authorization header
static inline bool davBasicAuth(char* out, size_t n, const char* user, const char* pass) {
  char raw[160];
  int l = snprintf(raw, sizeof(raw), "%s:%s", user ? user : "", pass ? pass : "");
  if (l <= 1 || l >= (int)sizeof(raw) || n < 8) return false;
  memcpy(out, "Basic ", 6);
  return davBase64(out + 6, n - 6, (const uint8_t*)raw, (size_t)l) > 0;
}

// "abcd..." for logs
static inline void davMask(char* out, size_t n, const char* secret) {
  if (!secret || !secret[0]) snprintf(out, n, "(none)");
  else snprintf(out, n, "(%u chars)", (unsigned)strlen(secret));
}

// --- Request bodies ----------------------------------------------------------------

static const char DAV_BODY_PRINCIPAL[] =
  "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
  "<d:propfind xmlns:d=\"DAV:\"><d:prop><d:current-user-principal/></d:prop></d:propfind>";

// Both home sets in one go: a server without contacts (or calendars) answers
// the other one in a 404 propstat, which the reader ignores
static const char DAV_BODY_CAL_HOME[] =
  "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
  "<d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\" "
  "xmlns:r=\"urn:ietf:params:xml:ns:carddav\">"
  "<d:prop><c:calendar-home-set/><r:addressbook-home-set/></d:prop></d:propfind>";

static const char DAV_BODY_CAL_LIST[] =
  "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
  "<d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\" "
  "xmlns:a=\"http://apple.com/ns/ical/\" xmlns:cs=\"http://calendarserver.org/ns/\">"
  "<d:prop><d:resourcetype/><d:displayname/><a:calendar-color/><cs:getctag/><d:sync-token/>"
  "<c:supported-calendar-component-set/></d:prop></d:propfind>";

// Has an address book changed since the last sync (depth 0)
static const char DAV_BODY_BOOK_STATE[] =
  "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
  "<d:propfind xmlns:d=\"DAV:\" xmlns:cs=\"http://calendarserver.org/ns/\">"
  "<d:prop><cs:getctag/><d:sync-token/></d:prop></d:propfind>";

// The cards in an address book (depth 1): what to fetch, and how big it is
static const char DAV_BODY_BOOK_LIST[] =
  "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
  "<d:propfind xmlns:d=\"DAV:\"><d:prop><d:resourcetype/><d:getetag/><d:getcontentlength/>"
  "</d:prop></d:propfind>";

// addressbook-multiget for up to n hrefs (paths, XML-escaped here). Asks for
// the name and numbers only; most servers send the whole card anyway. Returns
// the body length, 0 when it does not fit.
static inline size_t davMultigetBody(char* out, size_t cap, const char* const* hrefs, int n) {
  static const char head[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<r:addressbook-multiget xmlns:d=\"DAV:\" xmlns:r=\"urn:ietf:params:xml:ns:carddav\">"
    "<d:prop><d:getetag/><r:address-data><r:prop name=\"FN\"/><r:prop name=\"N\"/>"
    "<r:prop name=\"ORG\"/><r:prop name=\"TEL\"/></r:address-data></d:prop>";
  static const char tail[] = "</r:addressbook-multiget>";
  if (!out || cap < sizeof(head) + sizeof(tail)) return 0;
  size_t o = 0;
  memcpy(out, head, sizeof(head) - 1);
  o = sizeof(head) - 1;
  for (int k = 0; k < n; k++) {
    if (o + 16 >= cap) return 0;
    memcpy(out + o, "<d:href>", 8); o += 8;
    for (const char* p = hrefs[k]; *p; p++) {
      const char* e = *p == '&' ? "&amp;" : *p == '<' ? "&lt;" : *p == '>' ? "&gt;" : nullptr;
      size_t el = e ? strlen(e) : 1;
      if (o + el + 16 >= cap) return 0;
      if (e) { memcpy(out + o, e, el); o += el; }
      else out[o++] = *p;
    }
    memcpy(out + o, "</d:href>", 9); o += 9;
  }
  if (o + sizeof(tail) > cap) return 0;
  memcpy(out + o, tail, sizeof(tail));   // with the 0
  return o + sizeof(tail) - 1;
}

// Every change in a calendar since the (empty) token = the whole listing, etags only
static const char DAV_BODY_SYNC_ALL[] =
  "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
  "<d:sync-collection xmlns:d=\"DAV:\"><d:sync-token/><d:sync-level>1</d:sync-level>"
  "<d:prop><d:getetag/></d:prop></d:sync-collection>";

// --- XML ----------------------------------------------------------------------------

struct DavResponse {
  char href[200];
  char etag[80];
  char name[64];          // displayname
  char color[16];         // calendar-color, "#RRGGBB" (alpha dropped)
  char ctag[80];
  char syncToken[120];
  char principal[200];    // current-user-principal / href
  char home[200];         // calendar-home-set / href
  char abHome[200];       // addressbook-home-set / href
  long contentLength;     // getcontentlength, -1 = not given
  const char* data;       // address-data / calendar-data: raw text in the reply (still
  size_t dataLen;         //   XML-escaped, or a CDATA section), not copied
  int status;             // status of the response itself (sync-collection: 404 = deleted), 0 = none
  bool isCollection, isCalendar, isAddressbook;
  bool hasVevent, hasVtodo, hasComps;   // supported-calendar-component-set seen / lists VEVENT / VTODO
};

typedef void (*DavResponseFn)(const DavResponse& r, void* ctx);

// Local name of a tag ("D:href" -> "href"), lower case, into out
static inline void davXmlLocal(const char* s, size_t len, char* out, size_t n) {
  const char* colon = (const char*)memchr(s, ':', len);
  if (colon) { len -= (size_t)(colon + 1 - s); s = colon + 1; }
  if (len >= n) len = n - 1;
  for (size_t i = 0; i < len; i++) out[i] = (char)tolower((unsigned char)s[i]);
  out[len] = 0;
}

// Appends the UTF-8 form of a code point
static inline size_t davUtf8(char* out, uint32_t cp) {
  if (cp < 0x80) { out[0] = (char)cp; return 1; }
  if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 63)); return 2; }
  if (cp < 0x10000) {
    out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 63));
    out[2] = (char)(0x80 | (cp & 63)); return 3;
  }
  out[0] = (char)(0xF0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 63));
  out[2] = (char)(0x80 | ((cp >> 6) & 63)); out[3] = (char)(0x80 | (cp & 63)); return 4;
}

// Text between tags with entities decoded, CDATA kept as it is, white space
// at both ends dropped, into out (cut to fit)
static inline void davXmlText(const char* s, size_t len, char* out, size_t n) {
  size_t o = 0;
  if (n == 0) return;
  for (size_t i = 0; i < len && o + 5 < n;) {
    if (s[i] == '&') {
      const char* semi = (const char*)memchr(s + i, ';', len - i);
      if (semi && semi - (s + i) <= 10) {
        size_t el = (size_t)(semi - (s + i)) - 1;
        const char* e = s + i + 1;
        if (el == 3 && !strncmp(e, "amp", 3)) { out[o++] = '&'; i += 5; continue; }
        if (el == 2 && !strncmp(e, "lt", 2)) { out[o++] = '<'; i += 4; continue; }
        if (el == 2 && !strncmp(e, "gt", 2)) { out[o++] = '>'; i += 4; continue; }
        if (el == 4 && !strncmp(e, "quot", 4)) { out[o++] = '"'; i += 6; continue; }
        if (el == 4 && !strncmp(e, "apos", 4)) { out[o++] = '\''; i += 6; continue; }
        if (el >= 2 && e[0] == '#') {
          char num[12];
          size_t nl = el - 1 < sizeof(num) - 1 ? el - 1 : sizeof(num) - 1;
          memcpy(num, e + 1, nl);
          num[nl] = 0;
          uint32_t cp = (num[0] == 'x' || num[0] == 'X') ? (uint32_t)strtoul(num + 1, nullptr, 16)
                                                         : (uint32_t)strtoul(num, nullptr, 10);
          if (cp && cp < 0x110000) { o += davUtf8(out + o, cp); i += el + 2; continue; }
        }
      }
    }
    if (s[i] == '<' && len - i >= 9 && !strncmp(s + i, "<![CDATA[", 9)) {
      i += 9;
      while (i < len && !(len - i >= 3 && !strncmp(s + i, "]]>", 3)) && o + 1 < n) out[o++] = s[i++];
      if (len - i >= 3) i += 3;
      continue;
    }
    out[o++] = s[i++];
  }
  out[o] = 0;
  while (o && isspace((unsigned char)out[o - 1])) out[--o] = 0;
  size_t lead = 0;
  while (out[lead] && isspace((unsigned char)out[lead])) lead++;
  if (lead) memmove(out, out + lead, o - lead + 1);
}

// "HTTP/1.1 404 Not Found" -> 404, 0 when it does not look like a status line
static inline int davStatusCode(const char* s) {
  if (strncmp(s, "HTTP/", 5) != 0) return 0;
  const char* sp = strchr(s, ' ');
  return sp ? atoi(sp + 1) : 0;
}

// Value of attribute `name` in a start tag's text (between "<" and ">"), "" if absent
static inline bool davXmlAttr(const char* tag, size_t len, const char* name, char* out, size_t n) {
  out[0] = 0;
  size_t nl = strlen(name);
  for (size_t i = 0; i + nl + 2 < len; i++) {
    if ((i == 0 || isspace((unsigned char)tag[i - 1])) && !strncmp(tag + i, name, nl) &&
        (tag[i + nl] == '=' || isspace((unsigned char)tag[i + nl]))) {
      size_t j = i + nl;
      while (j < len && (isspace((unsigned char)tag[j]) || tag[j] == '=')) j++;
      if (j >= len || (tag[j] != '"' && tag[j] != '\'')) return false;
      char q = tag[j++];
      size_t e = j;
      while (e < len && tag[e] != q) e++;
      davXmlText(tag + j, e - j, out, n);
      return true;
    }
  }
  return false;
}

// A string cut at a byte limit may end in half a UTF-8 character: drop it
static inline void davTrimUtf8(char* s) {
  size_t l = strlen(s);
  size_t i = l;
  while (i > 0 && ((unsigned char)s[i - 1] & 0xC0) == 0x80) i--;   // continuation bytes
  if (i == 0) return;
  unsigned char lead = (unsigned char)s[i - 1];
  size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
  if (lead >= 0xC0 && l - (i - 1) < need) s[i - 1] = 0;
}

static inline void davCopy(char* dst, size_t n, const char* src) {
  if (n) snprintf(dst, n, "%s", src);
}

// Reads a multistatus (or any DAV reply made of <response> elements) and calls
// fn for each <response>. A property only counts when its propstat is 2xx, so
// the 404 block a server adds for properties it does not have is ignored.
// `topSyncToken` (may be null) gets the sync-token that is a direct child of
// <multistatus>. Returns the number of responses, -1 when the text is not XML
// of that shape.
static inline int davParseMultistatus(const char* xml, size_t len, DavResponseFn fn, void* ctx,
                                      char* topSyncToken = nullptr, size_t topN = 0) {
  if (!xml) return -1;
  if (topSyncToken && topN) topSyncToken[0] = 0;
  const int MAXD = 20;
  char stack[MAXD][40];
  int depth = 0;
  DavResponse resp, ps;
  memset(&resp, 0, sizeof(resp));
  memset(&ps, 0, sizeof(ps));
  int psStatus = 0, count = 0;
  bool seenMulti = false;
  size_t i = 0;
  while (i < len) {
    if (xml[i] != '<') {   // text up to the next tag
      // Leaf text is read at its start tag (below), so just skip it here
      while (i < len && xml[i] != '<') i++;
      continue;
    }
    if (len - i >= 4 && !strncmp(xml + i, "<!--", 4)) {
      const char* e = strstr(xml + i + 4, "-->");
      if (!e || (size_t)(e - xml) + 3 > len) return -1;
      i = (size_t)(e - xml) + 3;
      continue;
    }
    if (len - i >= 9 && !strncmp(xml + i, "<![CDATA[", 9)) {
      const char* e = strstr(xml + i + 9, "]]>");
      if (!e) return -1;
      i = (size_t)(e - xml) + 3;
      continue;
    }
    if (i + 1 < len && (xml[i + 1] == '?' || xml[i + 1] == '!')) {
      while (i < len && xml[i] != '>') i++;
      i++;
      continue;
    }
    size_t tagEnd = i + 1;
    char quote = 0;
    while (tagEnd < len && (quote || xml[tagEnd] != '>')) {
      if (quote) { if (xml[tagEnd] == quote) quote = 0; }
      else if (xml[tagEnd] == '"' || xml[tagEnd] == '\'') quote = xml[tagEnd];
      tagEnd++;
    }
    if (tagEnd >= len) return seenMulti ? count : -1;   // cut off
    bool closing = xml[i + 1] == '/';
    bool selfClose = xml[tagEnd - 1] == '/';
    size_t ns = i + 1 + (closing ? 1 : 0);
    size_t ne = ns;
    while (ne < tagEnd && !isspace((unsigned char)xml[ne]) && xml[ne] != '/') ne++;
    char name[40];
    davXmlLocal(xml + ns, ne - ns, name, sizeof(name));

    if (!closing) {
      const char* parent = depth ? stack[depth - 1] : "";
      if (depth == 0 && !strcmp(name, "multistatus")) seenMulti = true;
      if (depth == 0 && !seenMulti) return -1;   // not a multistatus
      if (!strcmp(name, "response")) { memset(&resp, 0, sizeof(resp)); resp.contentLength = -1; psStatus = 0; }
      else if (!strcmp(name, "propstat")) { memset(&ps, 0, sizeof(ps)); ps.contentLength = -1; psStatus = 0; }
      else if (!strcmp(name, "collection") && !strcmp(parent, "resourcetype")) ps.isCollection = true;
      else if (!strcmp(name, "calendar") && !strcmp(parent, "resourcetype")) ps.isCalendar = true;
      else if (!strcmp(name, "addressbook") && !strcmp(parent, "resourcetype")) ps.isAddressbook = true;
      else if (!strcmp(name, "supported-calendar-component-set")) ps.hasComps = true;
      else if (!strcmp(name, "comp") && !strcmp(parent, "supported-calendar-component-set")) {
        char v[16];
        davXmlAttr(xml + i, tagEnd - i, "name", v, sizeof(v));
        if (!strcasecmp(v, "VEVENT")) ps.hasVevent = true;
        else if (!strcasecmp(v, "VTODO")) ps.hasVtodo = true;
      }
      if (!selfClose) {
        if (depth >= MAXD) return -1;
        davCopy(stack[depth], sizeof(stack[depth]), name);
        // A card or an event: kept as a pointer into the reply, it can be large
        if (!strcmp(name, "address-data") || !strcmp(name, "calendar-data")) {
          size_t ds = tagEnd + 1, de = ds;
          if (len - ds >= 9 && !strncmp(xml + ds, "<![CDATA[", 9)) {
            const char* e = strstr(xml + ds + 9, "]]>");
            if (!e || (size_t)(e - xml) > len) return seenMulti ? count : -1;
            de = (size_t)(e - xml) + 3;
          } else {
            while (de < len && xml[de] != '<') de++;
          }
          ps.data = xml + ds;
          ps.dataLen = de - ds;
        }
        // Text of a leaf: from after this tag to the next one
        size_t ts = tagEnd + 1, te = ts;
        while (te < len && xml[te] != '<') te++;
        char text[220];
        if (te > ts) davXmlText(xml + ts, te - ts, text, sizeof(text));
        else text[0] = 0;
        // Direct text only matters for leaves; a container's text is white space
        if (text[0]) {
          if (!strcmp(name, "href")) {
            if (!strcmp(parent, "response")) davCopy(resp.href, sizeof(resp.href), text);
            else if (!strcmp(parent, "current-user-principal")) davCopy(ps.principal, sizeof(ps.principal), text);
            else if (!strcmp(parent, "calendar-home-set")) davCopy(ps.home, sizeof(ps.home), text);
            else if (!strcmp(parent, "addressbook-home-set")) davCopy(ps.abHome, sizeof(ps.abHome), text);
          } else if (!strcmp(name, "getetag")) davCopy(ps.etag, sizeof(ps.etag), text);
          else if (!strcmp(name, "getcontentlength")) ps.contentLength = atol(text);
          else if (!strcmp(name, "displayname")) davCopy(ps.name, sizeof(ps.name), text);
          else if (!strcmp(name, "calendar-color")) {
            // "#RRGGBBAA" -> "#RRGGBB"
            size_t cl = strlen(text);
            if (text[0] == '#' && cl >= 7) { memcpy(ps.color, text, 7); ps.color[7] = 0; }
          } else if (!strcmp(name, "getctag")) davCopy(ps.ctag, sizeof(ps.ctag), text);
          else if (!strcmp(name, "sync-token")) {
            if (!strcmp(parent, "multistatus")) {
              if (topSyncToken && topN) davCopy(topSyncToken, topN, text);
            } else davCopy(ps.syncToken, sizeof(ps.syncToken), text);
          } else if (!strcmp(name, "status")) {
            int code = davStatusCode(text);
            if (!strcmp(parent, "propstat")) psStatus = code;
            else if (!strcmp(parent, "response")) resp.status = code;
          }
        }
        depth++;
      } else if (!strcmp(name, "propstat")) {   // <propstat/> with nothing in it
        memset(&ps, 0, sizeof(ps));
      }
    } else {
      if (depth > 0) depth--;
      if (!strcmp(name, "propstat")) {
        if (psStatus >= 200 && psStatus < 300) {
          if (ps.etag[0]) davCopy(resp.etag, sizeof(resp.etag), ps.etag);
          if (ps.name[0]) davCopy(resp.name, sizeof(resp.name), ps.name);
          if (ps.color[0]) davCopy(resp.color, sizeof(resp.color), ps.color);
          if (ps.ctag[0]) davCopy(resp.ctag, sizeof(resp.ctag), ps.ctag);
          if (ps.syncToken[0]) davCopy(resp.syncToken, sizeof(resp.syncToken), ps.syncToken);
          if (ps.principal[0]) davCopy(resp.principal, sizeof(resp.principal), ps.principal);
          if (ps.home[0]) davCopy(resp.home, sizeof(resp.home), ps.home);
          if (ps.abHome[0]) davCopy(resp.abHome, sizeof(resp.abHome), ps.abHome);
          if (ps.contentLength >= 0) resp.contentLength = ps.contentLength;
          if (ps.data) { resp.data = ps.data; resp.dataLen = ps.dataLen; }
          resp.isCollection |= ps.isCollection;
          resp.isCalendar |= ps.isCalendar;
          resp.isAddressbook |= ps.isAddressbook;
          resp.hasComps |= ps.hasComps;
          resp.hasVevent |= ps.hasVevent;
          resp.hasVtodo |= ps.hasVtodo;
        }
      } else if (!strcmp(name, "response")) {
        if (fn) fn(resp, ctx);
        count++;
      }
    }
    i = tagEnd + 1;
  }
  return seenMulti ? count : -1;
}

// A multistatus that arrived whole ends in its closing tag, or is one empty
// element (`<multistatus xmlns="DAV:" />`: a calendar with nothing in the time
// range); one cut off on the way (a TLS error halfway) must not count as the
// whole list
static inline bool davMultistatusWhole(const char* xml) {
  if (!xml) return false;
  const char* end = xml + strlen(xml);
  while (end > xml && isspace((unsigned char)end[-1])) end--;
  if (end - xml < 14 || end[-1] != '>') return false;
  const char* lt = end - 1;
  while (lt > xml && *lt != '<') lt--;
  if (*lt != '<') return false;
  const char* name = lt + 1;
  bool closing = *name == '/';
  if (closing) name++;
  else if (end[-2] != '/') return false;   // an opening tag last: cut off
  size_t nl = strcspn(name, " \t\r\n/>");
  char local[40];
  davXmlLocal(name, nl, local, sizeof(local));
  return !strcmp(local, "multistatus");
}

// --- Phone numbers -------------------------------------------------------------------

// A number as the modem shows it: "+420 777-12 34 56" / "tel:00420777123456"
// -> "+420777123456". Digits and one leading +, "00" as +, an extension
// (";ext=", "x", "p", "w") dropped. False when fewer than 3 digits are left
// or it does not fit.
static inline bool davPhoneNorm(const char* in, char* out, size_t n) {
  if (!in || !out || n < 4) return false;
  out[0] = 0;
  while (*in == ' ' || *in == '\t') in++;
  if (!strncasecmp(in, "tel:", 4)) in += 4;
  size_t o = 0;
  int digits = 0;
  for (const char* p = in; *p; p++) {
    char c = *p;
    if (c >= '0' && c <= '9') {
      if (o + 1 >= n) { out[0] = 0; return false; }
      out[o++] = c;
      digits++;
    } else if (c == '+' && o == 0) {
      out[o++] = '+';
    } else if (c == ';' || c == ',' || c == 'x' || c == 'X' || c == 'p' || c == 'P' || c == 'w' || c == 'W') {
      break;
    }
  }
  out[o] = 0;
  if (o >= 2 && out[0] == '0' && out[1] == '0') {   // 00420... -> +420...
    out[0] = '+';
    memmove(out + 1, out + 2, o - 1);
    o--;
  }
  if (digits < 3) { out[0] = 0; return false; }
  return true;
}

// The same number written with or without the country code: equal after
// davPhoneNorm, or the last 9 digits equal when both have at least 9
// (the national number length in much of Europe; shorter ones must match whole)
static inline bool davPhoneSame(const char* a, const char* b) {
  char na[24], nb[24];
  if (!davPhoneNorm(a, na, sizeof(na)) || !davPhoneNorm(b, nb, sizeof(nb))) return false;
  if (!strcmp(na, nb)) return true;
  const char* da = na[0] == '+' ? na + 1 : na;
  const char* db = nb[0] == '+' ? nb + 1 : nb;
  size_t la = strlen(da), lb = strlen(db);
  if (la < 9 || lb < 9) return false;
  return !strcmp(da + la - 9, db + lb - 9);
}

// --- vCard ---------------------------------------------------------------------------

// The text of an address-data element as the reply has it: XML entities (or
// a CDATA section) undone byte by byte, CR dropped
struct DavRawText {
  const char* s;
  size_t len, i;
  bool cdata;
  char pend[4];
  int npend, ipend, peekc;
  DavRawText(const char* str, size_t n) : s(str), len(n), i(0), cdata(false), npend(0), ipend(0), peekc(-2) {
    if (len >= 9 && !strncmp(s, "<![CDATA[", 9)) {
      cdata = true;
      i = 9;
      if (len >= 12 && !strncmp(s + len - 3, "]]>", 3)) len -= 3;
    }
  }
  int raw() {   // next byte, -1 at the end
    if (ipend < npend) return (unsigned char)pend[ipend++];
    while (i < len) {
      char c = s[i];
      if (!cdata && c == '&') {
        const char* semi = (const char*)memchr(s + i, ';', len - i < 12 ? len - i : 12);
        if (semi) {
          size_t el = (size_t)(semi - (s + i)) - 1;
          const char* e = s + i + 1;
          uint32_t cp = 0;
          if (el == 3 && !strncmp(e, "amp", 3)) cp = '&';
          else if (el == 2 && !strncmp(e, "lt", 2)) cp = '<';
          else if (el == 2 && !strncmp(e, "gt", 2)) cp = '>';
          else if (el == 4 && !strncmp(e, "quot", 4)) cp = '"';
          else if (el == 4 && !strncmp(e, "apos", 4)) cp = '\'';
          else if (el >= 2 && e[0] == '#') {
            char num[12];
            size_t nl = el - 1 < sizeof(num) - 1 ? el - 1 : sizeof(num) - 1;
            memcpy(num, e + 1, nl);
            num[nl] = 0;
            cp = (num[0] == 'x' || num[0] == 'X') ? (uint32_t)strtoul(num + 1, nullptr, 16)
                                                  : (uint32_t)strtoul(num, nullptr, 10);
            if (cp >= 0x110000) cp = 0;
          }
          if (cp) {
            i += el + 2;
            if (cp == '\r') continue;
            npend = (int)davUtf8(pend, cp);
            ipend = 1;
            return (unsigned char)pend[0];
          }
        }
      }
      i++;
      if (c == '\r') continue;
      return (unsigned char)c;
    }
    return -1;
  }
  int get() { if (peekc != -2) { int c = peekc; peekc = -2; return c; } return raw(); }
  int peek() { if (peekc == -2) peekc = raw(); return peekc; }
};

// Whether the parameter part of a line (before its first ':') asks for
// quoted-printable (vCard 2.1)
static inline bool davVcardIsQp(const char* line, size_t upto) {
  for (size_t k = 0; k + 16 <= upto; k++)
    if (!strncasecmp(line + k, "QUOTED-PRINTABLE", 16)) return true;
  return false;
}

// One unfolded content line ("TEL;TYPE=cell:+420 ..."), cut at n-1 bytes (a
// photo line is long and of no use here). False at the end of the text.
static inline bool davVcardLine(DavRawText& t, char* out, size_t n) {
  size_t o = 0;
  int c = t.get();
  if (c < 0) return false;
  for (;;) {
    if (c < 0) break;
    if (c == '\n') {
      int nx = t.peek();
      if (nx == ' ' || nx == '\t') { t.get(); c = t.get(); continue; }   // a folded line goes on
      // vCard 2.1 quoted-printable: a soft line break is "=" at the end
      if (o && out[o - 1] == '=') {
        const char* colon = (const char*)memchr(out, ':', o);
        if (colon && davVcardIsQp(out, (size_t)(colon - out))) { o--; c = t.get(); continue; }
      }
      break;
    }
    if (o + 1 < n) out[o++] = (char)c;
    c = t.get();
  }
  out[o] = 0;
  return true;
}

// A value with vCard escapes (\n \, \; \\) undone, and quoted-printable
// decoded when the parameters say so; in place
static inline void davVcardValue(char* v, bool qp) {
  char* o = v;
  for (char* p = v; *p; p++) {
    if (qp && *p == '=' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
      char h[3] = { p[1], p[2], 0 };
      *o++ = (char)strtoul(h, nullptr, 16);
      p += 2;
    } else if (*p == '\\' && p[1]) {
      p++;
      *o++ = (*p == 'n' || *p == 'N') ? ' ' : *p;
    } else {
      *o++ = *p;
    }
  }
  *o = 0;
}

// The idx-th ';'-separated part of a structured value (N, ORG); "\;" is not
// a separator and stays escaped for davVcardValue
static inline void davVcardPart(const char* v, int idx, char* out, size_t n) {
  out[0] = 0;
  int k = 0;
  size_t o = 0;
  for (const char* p = v; *p; p++) {
    if (*p == '\\' && p[1]) {
      if (k == idx && o + 2 < n) { out[o++] = p[0]; out[o++] = p[1]; }
      p++;
      continue;
    }
    if (*p == ';') { if (k == idx) break; k++; continue; }
    if (k == idx && o + 1 < n) out[o++] = *p;
  }
  out[o] = 0;
}

static inline void davTrimSpaces(char* s) {
  size_t l = strlen(s);
  while (l && isspace((unsigned char)s[l - 1])) s[--l] = 0;
  size_t lead = 0;
  while (s[lead] && isspace((unsigned char)s[lead])) lead++;
  if (lead) memmove(s, s + lead, l - lead + 1);
}

typedef void (*DavContactFn)(const char* name, const char* phone, void* ctx);

// Reads the cards in an address-data text (one or more BEGIN:VCARD ...
// END:VCARD) and calls fn once per phone number with the card's name: FN,
// else "Given Family" from N, else the organisation, else the number itself.
// Cards without a number are skipped. Returns the number of cards read.
static inline int davVcardParse(const char* data, size_t len, DavContactFn fn, void* ctx) {
  if (!data) return 0;
  DavRawText t(data, len);
  const int MAXTEL = 8;
  char line[320];
  char fnName[96], nName[96], org[96];
  char tels[MAXTEL][24];
  int ntel = 0, cards = 0;
  bool in = false;
  fnName[0] = nName[0] = org[0] = 0;
  while (davVcardLine(t, line, sizeof(line))) {
    // name[;params]:value, maybe with a group ("item1.TEL")
    char* colon = nullptr;
    bool quoted = false;
    for (char* p = line; *p; p++) {
      if (*p == '"') quoted = !quoted;
      else if (*p == ':' && !quoted) { colon = p; break; }
    }
    if (!colon) continue;
    bool qp = davVcardIsQp(line, (size_t)(colon - line));
    *colon = 0;
    char* value = colon + 1;
    char* name = line;
    char* params = strchr(name, ';');
    if (params) *params = 0;
    char* dot = strchr(name, '.');
    if (dot) name = dot + 1;

    if (!strcasecmp(name, "BEGIN") && !strncasecmp(value, "VCARD", 5)) {
      in = true;
      fnName[0] = nName[0] = org[0] = 0;
      ntel = 0;
    } else if (!in) {
      continue;
    } else if (!strcasecmp(name, "END") && !strncasecmp(value, "VCARD", 5)) {
      in = false;
      cards++;
      for (int k = 0; k < ntel && fn; k++) {
        const char* nm = fnName[0] ? fnName : nName[0] ? nName : org[0] ? org : tels[k];
        fn(nm, tels[k], ctx);
      }
    } else if (!strcasecmp(name, "FN")) {
      davVcardValue(value, qp);
      davCopy(fnName, sizeof(fnName), value);
      davTrimSpaces(fnName);
    } else if (!strcasecmp(name, "N")) {
      char fam[48], giv[48];
      davVcardPart(value, 0, fam, sizeof(fam));
      davVcardPart(value, 1, giv, sizeof(giv));
      davVcardValue(fam, qp);
      davVcardValue(giv, qp);
      davTrimSpaces(fam);
      davTrimSpaces(giv);
      snprintf(nName, sizeof(nName), "%s%s%s", giv, giv[0] && fam[0] ? " " : "", fam);
    } else if (!strcasecmp(name, "ORG")) {
      davVcardPart(value, 0, org, sizeof(org));
      davVcardValue(org, qp);
      davTrimSpaces(org);
    } else if (!strcasecmp(name, "TEL") && ntel < MAXTEL) {
      davVcardValue(value, qp);
      if (davPhoneNorm(value, tels[ntel], sizeof(tels[ntel]))) {
        bool dup = false;
        for (int k = 0; k < ntel && !dup; k++) dup = !strcmp(tels[k], tels[ntel]);
        if (!dup) ntel++;
      }
    }
  }
  return cards;
}

// --- iCalendar events (the lock screen agenda) ---------------------------------------

// "20261113T080000Z" for an RTC time (UTC); what CalDAV's time-range and
// expand take
static inline void davIcalUtcStamp(uint32_t t, char* out, size_t n) {
  uint32_t days = t / 86400, sod = t % 86400;
  // civil_from_days (H. Hinnant), days since 1970-01-01
  int32_t z = (int32_t)days + 719468;
  int32_t era = z / 146097;
  uint32_t doe = (uint32_t)(z - era * 146097);
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int32_t y = (int32_t)yoe + era * 400;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  uint32_t d = doy - (153 * mp + 2) / 5 + 1;
  uint32_t m = mp < 10 ? mp + 3 : mp - 9;
  if (m <= 2) y++;
  snprintf(out, n, "%04ld%02lu%02luT%02lu%02lu%02luZ", (long)y, (unsigned long)m, (unsigned long)d,
           (unsigned long)(sod / 3600), (unsigned long)(sod / 60 % 60), (unsigned long)(sod % 60));
}

// Days since 1970-01-01 of a civil date (days_from_civil)
static inline int32_t davDaysFromCivil(int32_t y, uint32_t m, uint32_t d) {
  y -= m <= 2;
  int32_t era = (y >= 0 ? y : y - 399) / 400;
  uint32_t yoe = (uint32_t)(y - era * 400);
  uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

// Civil date of a day number (days since 1970-01-01), civil_from_days
static inline void davCivil(int64_t days, int32_t& y, uint32_t& m, uint32_t& d) {
  int64_t z = days + 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  uint32_t doe = (uint32_t)(z - era * 146097);
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y = (int32_t)(yoe + era * 400) + (m <= 2);
}

// Day of the week of a day number, 0 = Monday
static inline int davWeekday(int64_t days) { return (int)(((days + 3) % 7 + 7) % 7); }

static inline int davDaysInMonth(int32_t y, uint32_t m) {
  return m == 12 ? 31 : davDaysFromCivil(y, m + 1, 1) - davDaysFromCivil(y, m, 1);
}

// calendar-query for the events that touch [startUtc, endUtc), as stored:
// a recurring event comes whole (its rule, exceptions and moved instances),
// the deck expands it (davIcalParse). Not the server's expand: Radicale 3.9
// gives the instances of a series that crosses a daylight saving change the
// offset of its first one, and then misses its EXDATEs and moved instances.
// Returns the body length, 0 when it does not fit.
static inline size_t davCalQueryBody(char* out, size_t cap, uint32_t startUtc, uint32_t endUtc) {
  char s[20], e[20];
  davIcalUtcStamp(startUtc, s, sizeof(s));
  davIcalUtcStamp(endUtc, e, sizeof(e));
  int l = snprintf(out, cap,
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<c:calendar-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
    "<d:prop><d:getetag/><c:calendar-data/></d:prop>"
    "<c:filter><c:comp-filter name=\"VCALENDAR\"><c:comp-filter name=\"VEVENT\">"
    "<c:time-range start=\"%s\" end=\"%s\"/></c:comp-filter></c:comp-filter></c:filter>"
    "</c:calendar-query>", s, e);
  return (l > 0 && (size_t)l < cap) ? (size_t)l : 0;
}

struct DavEvent {
  uint32_t start, end;   // RTC seconds (UTC); an all-day event from the deck's local midnight
  bool allDay;
  char title[48];
};

typedef void (*DavEventFn)(const DavEvent& e, void* ctx);

// A date-time as written: "20261113T090000Z" (UTC), "20261113T090000"
// (floating, or with a TZID) or "20261113" (a date), as seconds of that
// wall-clock time counted as if it were UTC ("naive"). With a TZID or
// floating it is the wall clock of the event's zone, which the deck takes as
// its own (UTC + its offset): a weekly 17:00 stays 17:00 across a daylight
// saving change, as on the phone. False when it is none of these.
static inline bool davIcalNaive(const char* v, int64_t& out, bool& isDate, bool& utc) {
  int n = 0;
  while (isdigit((unsigned char)v[n])) n++;
  if (n != 8) return false;
  int32_t y = (v[0] - '0') * 1000 + (v[1] - '0') * 100 + (v[2] - '0') * 10 + (v[3] - '0');
  uint32_t mo = (uint32_t)((v[4] - '0') * 10 + (v[5] - '0'));
  uint32_t d = (uint32_t)((v[6] - '0') * 10 + (v[7] - '0'));
  if (y < 1900 || y > 2105 || mo < 1 || mo > 12 || d < 1 || d > 31) return false;
  out = (int64_t)davDaysFromCivil(y, mo, d) * 86400;
  isDate = v[8] != 'T';
  utc = false;
  if (!isDate) {
    const char* hp = v + 9;
    for (int k = 0; k < 6; k++) if (!isdigit((unsigned char)hp[k])) return false;
    out += ((hp[0] - '0') * 10 + (hp[1] - '0')) * 3600 + ((hp[2] - '0') * 10 + (hp[3] - '0')) * 60 +
           (hp[4] - '0') * 10 + (hp[5] - '0');
    utc = hp[6] == 'Z' || hp[6] == 'z';
  }
  return true;
}

// "PT1H30M", "P1D", "P1W": seconds, -1 when not a duration (a negative one
// included: an event does not end before it starts)
static inline long davIcalDuration(const char* v) {
  if (*v == '+') v++;
  if (*v != 'P' && *v != 'p') return -1;
  v++;
  long total = 0;
  bool time = false;
  while (*v) {
    if (*v == 'T' || *v == 't') { time = true; v++; continue; }
    if (!isdigit((unsigned char)*v)) return -1;
    long n = 0;
    while (isdigit((unsigned char)*v)) n = n * 10 + (*v++ - '0');
    char u = (char)toupper((unsigned char)*v++);
    if (u == 'W') total += n * 7 * 86400;
    else if (u == 'D') total += n * 86400;
    else if (u == 'H' && time) total += n * 3600;
    else if (u == 'M' && time) total += n * 60;
    else if (u == 'S' && time) total += n;
    else return -1;
  }
  return total;
}

// One VEVENT of a calendar-data text, before expansion
struct DavIcalComp {
  int64_t start, end, recur;   // naive (davIcalNaive)
  long dur;                    // DURATION, -1 none
  uint32_t uid;                // hash of UID
  bool hasStart, hasEnd, allDay, utc, endUtc, hasRecur, recurUtc, cancelled;
  uint8_t nex;
  int64_t ex[24];              // EXDATEs, naive
  bool exUtc[24];
  char rrule[160];
  char title[48];
};

// Room for the VEVENTs of one calendar object (a series and its moved
// instances); a caller keeps it off the stack (PSRAM on the deck)
static const int DAV_ICAL_MAX_COMPS = 40;
struct DavIcalWork {
  DavIcalComp c[DAV_ICAL_MAX_COMPS];
};

static inline uint32_t davFnv(const char* s) {
  uint32_t h = 2166136261u;
  for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; }
  return h;
}

// The parts of an RRULE the deck expands
struct DavRrule {
  enum Freq : uint8_t { NONE, DAILY, WEEKLY, MONTHLY, YEARLY };
  Freq freq = NONE;
  int interval = 1;
  long count = -1;             // -1: no COUNT
  int64_t until = INT64_MAX;   // naive, in the series' frame
  int nday = 0;
  int8_t dayWd[14], dayN[14];  // BYDAY: weekday (0 = MO) and ordinal (0: every)
  int nmday = 0;
  int8_t mday[31];             // BYMONTHDAY, negative from the end
  uint16_t months = 0;         // BYMONTH, bit m (1..12)
  bool unsupported = false;    // BYSETPOS, BYWEEKNO, BYYEARDAY, HOURLY...: first one only
};

// A time in the frame of a series: UTC and wall clock differ by the offset
static inline int64_t davToFrame(int64_t t, bool tUtc, bool frameUtc, int32_t off) {
  if (tUtc == frameUtc) return t;
  return tUtc ? t + off : t - off;
}

static inline void davParseRrule(const char* s, bool frameUtc, bool startIsDate, int32_t off, DavRrule& r) {
  char buf[160];
  davCopy(buf, sizeof(buf), s);
  char* save = nullptr;
  for (char* tok = strtok_r(buf, ";", &save); tok; tok = strtok_r(nullptr, ";", &save)) {
    char* eq = strchr(tok, '=');
    if (!eq) continue;
    *eq = 0;
    const char* k = tok;
    char* v = eq + 1;
    if (!strcasecmp(k, "FREQ")) {
      r.freq = !strcasecmp(v, "DAILY") ? DavRrule::DAILY : !strcasecmp(v, "WEEKLY") ? DavRrule::WEEKLY
             : !strcasecmp(v, "MONTHLY") ? DavRrule::MONTHLY : !strcasecmp(v, "YEARLY") ? DavRrule::YEARLY
             : DavRrule::NONE;
      if (r.freq == DavRrule::NONE) r.unsupported = true;
    } else if (!strcasecmp(k, "INTERVAL")) {
      r.interval = atoi(v);
      if (r.interval < 1) r.interval = 1;
    } else if (!strcasecmp(k, "COUNT")) {
      r.count = atol(v);
    } else if (!strcasecmp(k, "UNTIL")) {
      int64_t u; bool isDate, uUtc;
      if (davIcalNaive(v, u, isDate, uUtc)) {
        if (isDate) u += startIsDate ? 0 : 86399;   // the whole of that day
        r.until = davToFrame(u, uUtc, frameUtc, off);
      }
    } else if (!strcasecmp(k, "BYDAY")) {
      static const char* const wd[] = { "MO", "TU", "WE", "TH", "FR", "SA", "SU" };
      for (char* d = v; *d && r.nday < 14;) {
        char* end = strchr(d, ',');
        if (end) *end = 0;
        int n = atoi(d);   // "+1", "-1", "2" or nothing
        const char* name = d;
        while (*name == '+' || *name == '-' || isdigit((unsigned char)*name)) name++;
        for (int i = 0; i < 7; i++)
          if (!strncasecmp(name, wd[i], 2)) { r.dayWd[r.nday] = (int8_t)i; r.dayN[r.nday] = (int8_t)n; r.nday++; }
        if (!end) break;
        d = end + 1;
      }
    } else if (!strcasecmp(k, "BYMONTHDAY")) {
      for (char* p = v; p && r.nmday < 31;) {
        r.mday[r.nmday++] = (int8_t)atoi(p);
        p = strchr(p, ',');
        if (p) p++;
      }
    } else if (!strcasecmp(k, "BYMONTH")) {
      for (char* p = v; p;) {
        int m = atoi(p);
        if (m >= 1 && m <= 12) r.months |= (uint16_t)(1u << m);
        p = strchr(p, ',');
        if (p) p++;
      }
    } else if (!strcasecmp(k, "BYSETPOS") || !strcasecmp(k, "BYWEEKNO") || !strcasecmp(k, "BYYEARDAY") ||
               !strcasecmp(k, "BYHOUR") || !strcasecmp(k, "BYMINUTE") || !strcasecmp(k, "BYSECOND")) {
      r.unsupported = true;
    }
  }
}

// The days of month y/m a MONTHLY (or YEARLY, per month) rule gives, sorted;
// returns how many. With neither BYMONTHDAY nor BYDAY: the start's day.
static inline int davMonthDays(const DavRrule& r, int32_t y, uint32_t m, uint32_t startDay, uint8_t* out) {
  int dim = davDaysInMonth(y, m);
  bool hit[32] = {};
  int64_t first = davDaysFromCivil(y, m, 1);
  int fwd = davWeekday(first);
  if (r.nmday) {
    for (int i = 0; i < r.nmday; i++) {
      int d = r.mday[i] > 0 ? r.mday[i] : dim + r.mday[i] + 1;
      if (d < 1 || d > dim) continue;
      bool ok = r.nday == 0;
      for (int k = 0; k < r.nday && !ok; k++) ok = davWeekday(first + d - 1) == r.dayWd[k];
      if (ok) hit[d] = true;
    }
  } else if (r.nday) {
    for (int k = 0; k < r.nday; k++) {
      int d1 = 1 + ((r.dayWd[k] - fwd) + 7) % 7;   // first such weekday
      if (r.dayN[k] == 0) {
        for (int d = d1; d <= dim; d += 7) hit[d] = true;
      } else if (r.dayN[k] > 0) {
        int d = d1 + (r.dayN[k] - 1) * 7;
        if (d <= dim) hit[d] = true;
      } else {
        int last = d1 + ((dim - d1) / 7) * 7;
        int d = last + (r.dayN[k] + 1) * 7;
        if (d >= 1) hit[d] = true;
      }
    }
  } else if ((int)startDay <= dim) {
    hit[startDay] = true;
  }
  int n = 0;
  for (int d = 1; d <= dim; d++) if (hit[d]) out[n++] = (uint8_t)d;
  return n;
}

// Hands one instance (naive start in the series' frame) to fn when it
// touches [winStart, winEnd) (UTC; winEnd 0: no window)
static inline bool davIcalEmit(const DavIcalComp& c, int64_t naive, long dur, int32_t off,
                               uint32_t winStart, uint32_t winEnd, DavEventFn fn, void* ctx) {
  int64_t s = c.utc ? naive : naive - off;
  int64_t e = s + dur;
  if (s < 0 || e > 0xFFFFFFFFLL) return false;
  if (winEnd) {
    bool in = e > s ? (s < (int64_t)winEnd && e > (int64_t)winStart) : (s >= (int64_t)winStart && s < (int64_t)winEnd);
    if (!in) return false;
  }
  DavEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.start = (uint32_t)s;
  ev.end = (uint32_t)e;
  ev.allDay = c.allDay;
  davCopy(ev.title, sizeof(ev.title), c.title);
  if (fn) fn(ev, ctx);
  return true;
}

static inline long davIcalCompDur(const DavIcalComp& c, int32_t off) {
  long dur;
  if (c.hasEnd) dur = (long)(davToFrame(c.end, c.endUtc, c.utc, off) - c.start);
  else if (c.dur >= 0) dur = c.dur;
  else dur = c.allDay ? 86400 : 0;
  return dur < 0 ? 0 : dur;
}

// The instances of series `m` in the window: its rule from its start, less its
// EXDATEs and the instances moved elsewhere (RECURRENCE-ID of the same UID)
static inline int davIcalExpand(const DavIcalWork& w, int n, int mi, int32_t off,
                                uint32_t winStart, uint32_t winEnd, DavEventFn fn, void* ctx) {
  const DavIcalComp& m = w.c[mi];
  const long dur = davIcalCompDur(m, off);
  DavRrule r;
  davParseRrule(m.rrule, m.utc, m.allDay, off, r);
  if (r.unsupported || r.freq == DavRrule::NONE || !winEnd)
    return davIcalEmit(m, m.start, dur, off, winStart, winEnd, fn, ctx) ? 1 : 0;

  // The window in the series' frame; an instance ending in it counts too
  const int64_t wS = davToFrame(winStart, true, m.utc, off) - dur;
  const int64_t wE = davToFrame(winEnd, true, m.utc, off);
  const int64_t day0 = (m.start >= 0 ? m.start : m.start - 86399) / 86400;
  const int64_t tod = m.start - day0 * 86400;
  int32_t y0; uint32_t m0, d0;
  davCivil(day0, y0, m0, d0);

  int emitted = 0;
  long seen = 0;
  bool stop = false;
  // One candidate day of the rule, in order; false: the series ends here
  auto take = [&](int64_t day) -> bool {
    int64_t t = day * 86400 + tod;
    if (t < m.start) return true;
    if (r.count >= 0 && seen >= r.count) return false;
    if (t > r.until || t >= wE) return false;
    seen++;
    if (t < wS) return true;
    for (int i = 0; i < m.nex; i++) {
      int64_t x = davToFrame(m.ex[i], m.exUtc[i], m.utc, off);
      if (m.allDay ? x / 86400 == day : x == t) return true;
    }
    for (int i = 0; i < n; i++) {
      const DavIcalComp& o = w.c[i];
      if (!o.hasRecur || o.uid != m.uid) continue;
      int64_t x = davToFrame(o.recur, o.recurUtc, m.utc, off);
      if (m.allDay ? x / 86400 == day : x == t) return true;
    }
    if (davIcalEmit(m, t, dur, off, winStart, winEnd, fn, ctx)) emitted++;
    return true;
  };

  // Without COUNT the early periods are of no interest: start a little
  // before the window (the period stays aligned to the interval)
  const int64_t wDay = wS / 86400;
  if (r.freq == DavRrule::DAILY || r.freq == DavRrule::WEEKLY) {
    const int64_t step = (r.freq == DavRrule::DAILY ? 1 : 7) * (int64_t)r.interval;
    int64_t first = r.freq == DavRrule::DAILY ? day0 : day0 - davWeekday(day0);
    if (r.count < 0 && wDay - 7 > first) first += ((wDay - 7 - first) / step) * step;
    for (int64_t p = first, guard = 0; !stop && guard < 2000; p += step, guard++) {
      if (r.freq == DavRrule::DAILY) {
        int32_t y; uint32_t mo, d;
        davCivil(p, y, mo, d);
        bool ok = !r.months || (r.months & (1u << mo));
        if (ok && r.nmday) {
          int dim = davDaysInMonth(y, mo);
          ok = false;
          for (int i = 0; i < r.nmday && !ok; i++) ok = (r.mday[i] > 0 ? r.mday[i] : dim + r.mday[i] + 1) == (int)d;
        }
        if (ok && r.nday) {
          ok = false;
          for (int k = 0; k < r.nday && !ok; k++) ok = r.dayWd[k] == davWeekday(p);
        }
        if (ok) stop = !take(p);
      } else {
        for (int wd = 0; wd < 7 && !stop; wd++) {
          bool ok = false;
          if (!r.nday) ok = wd == davWeekday(day0);
          for (int k = 0; k < r.nday && !ok; k++) ok = r.dayWd[k] == wd;
          if (!ok) continue;
          if (r.months) {
            int32_t y; uint32_t mo, d;
            davCivil(p + wd, y, mo, d);
            if (!(r.months & (1u << mo))) continue;
          }
          stop = !take(p + wd);
        }
      }
    }
  } else if (r.freq == DavRrule::MONTHLY) {
    int64_t idx = (int64_t)y0 * 12 + (m0 - 1);
    if (r.count < 0) {
      int32_t wy; uint32_t wm, wd;
      davCivil(wDay, wy, wm, wd);
      int64_t widx = (int64_t)wy * 12 + (wm - 1) - 2;
      if (widx > idx) idx += ((widx - idx) / r.interval) * r.interval;
    }
    uint8_t days[31];
    for (int guard = 0; !stop && guard < 2000; idx += r.interval, guard++) {
      int32_t y = (int32_t)(idx / 12);
      uint32_t mo = (uint32_t)(idx % 12) + 1;
      if (r.months && !(r.months & (1u << mo))) continue;
      int k = davMonthDays(r, y, mo, d0, days);
      for (int i = 0; i < k && !stop; i++) stop = !take(davDaysFromCivil(y, mo, days[i]));
      if (davDaysFromCivil(y, mo, 1) * 86400LL > wE) break;
    }
  } else {   // YEARLY
    if (r.nday && !r.months) {   // "the 20th Monday of the year": not here
      return davIcalEmit(m, m.start, dur, off, winStart, winEnd, fn, ctx) ? 1 : 0;
    }
    int32_t y = y0;
    if (r.count < 0) {
      int32_t wy; uint32_t wm, wd;
      davCivil(wDay, wy, wm, wd);
      if (wy - 1 > y) y += ((wy - 1 - y) / r.interval) * r.interval;
    }
    uint8_t days[31];
    for (int guard = 0; !stop && guard < 500; y += r.interval, guard++) {
      for (uint32_t mo = 1; mo <= 12 && !stop; mo++) {
        if (r.months ? !(r.months & (1u << mo)) : mo != m0) continue;
        int k = davMonthDays(r, y, mo, d0, days);
        for (int i = 0; i < k && !stop; i++) stop = !take(davDaysFromCivil(y, mo, days[i]));
      }
      if (davDaysFromCivil(y, 1, 1) * 86400LL > wE) break;
    }
  }
  return emitted;
}

// Reads the VEVENTs in a calendar-data text and calls fn for every instance
// that touches [winStart, winEnd) (UTC): a recurring event is expanded on its
// rule (DAILY / WEEKLY / MONTHLY / YEARLY with INTERVAL, COUNT, UNTIL, BYDAY
// with ordinals, BYMONTHDAY, BYMONTH; anything else: its first one only),
// less its EXDATEs; a moved instance (RECURRENCE-ID) replaces the one it
// moves. Cancelled ones are left out, properties of nested components
// (VALARM) ignored. A missing end is the start (a date: the next day).
// Times with a TZID or floating are wall clock, taken as the deck's local
// time (UTC + utcOffsetSec). winEnd 0: no window, a series gives its first
// instance only. `w` holds the components (DAV_ICAL_MAX_COMPS; more are
// left out). Returns the number of events handed to fn.
static inline int davIcalParse(const char* data, size_t len, int32_t utcOffsetSec, uint32_t winStart,
                               uint32_t winEnd, DavIcalWork* w, DavEventFn fn, void* ctx) {
  if (!data || !w) return 0;
  DavRawText t(data, len);
  char line[512];
  int n = 0, nested = 0;
  bool in = false;
  DavIcalComp* c = nullptr;
  while (davVcardLine(t, line, sizeof(line))) {
    char* colon = nullptr;
    bool quoted = false;
    for (char* p = line; *p; p++) {
      if (*p == '"') quoted = !quoted;
      else if (*p == ':' && !quoted) { colon = p; break; }
    }
    if (!colon) continue;
    *colon = 0;
    char* value = colon + 1;
    char* name = line;
    char* params = strchr(name, ';');
    if (params) *params++ = 0;

    if (!strcasecmp(name, "BEGIN")) {
      if (in) nested++;
      else if (!strncasecmp(value, "VEVENT", 6)) {
        in = true;
        nested = 0;
        c = n < DAV_ICAL_MAX_COMPS ? &w->c[n] : nullptr;
        if (c) { memset(c, 0, sizeof(*c)); c->dur = -1; }
      }
    } else if (!in) {
      continue;
    } else if (!strcasecmp(name, "END")) {
      if (nested) { nested--; continue; }
      in = false;
      if (c && c->hasStart) n++;
      c = nullptr;
    } else if (nested || !c) {
      continue;
    } else if (!strcasecmp(name, "DTSTART")) {
      bool isDate;
      if (davIcalNaive(value, c->start, isDate, c->utc)) { c->hasStart = true; c->allDay = isDate; }
    } else if (!strcasecmp(name, "DTEND")) {
      bool isDate;
      if (davIcalNaive(value, c->end, isDate, c->endUtc)) c->hasEnd = true;
    } else if (!strcasecmp(name, "DURATION")) {
      c->dur = davIcalDuration(value);
    } else if (!strcasecmp(name, "RECURRENCE-ID")) {
      bool isDate;
      if (davIcalNaive(value, c->recur, isDate, c->recurUtc)) c->hasRecur = true;
    } else if (!strcasecmp(name, "RRULE")) {
      davCopy(c->rrule, sizeof(c->rrule), value);
    } else if (!strcasecmp(name, "EXDATE")) {
      for (char* p = value; p && *p && c->nex < 24;) {
        bool isDate;
        if (davIcalNaive(p, c->ex[c->nex], isDate, c->exUtc[c->nex])) c->nex++;
        p = strchr(p, ',');
        if (p) p++;
      }
    } else if (!strcasecmp(name, "UID")) {
      c->uid = davFnv(value);
    } else if (!strcasecmp(name, "STATUS")) {
      c->cancelled = !strncasecmp(value, "CANCELLED", 9);
    } else if (!strcasecmp(name, "SUMMARY")) {
      davVcardValue(value, false);
      davTrimSpaces(value);
      davCopy(c->title, sizeof(c->title), value);
      davTrimUtf8(c->title);
    }
  }

  int events = 0;
  for (int i = 0; i < n; i++) {
    const DavIcalComp& e = w->c[i];
    if (e.cancelled) continue;   // a cancelled moved instance still hides the one it moved
    if (e.rrule[0] && !e.hasRecur) events += davIcalExpand(*w, n, i, utcOffsetSec, winStart, winEnd, fn, ctx);
    else if (davIcalEmit(e, e.start, davIcalCompDur(e, utcOffsetSec), utcOffsetSec, winStart, winEnd, fn, ctx)) events++;
  }
  return events;
}
