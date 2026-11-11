#pragma once
// =============================================================================
// CalDAV/CardDAV client (docs/caldav-design.md) -- the parts without hardware
//
// URL handling, HTTP Basic, and a small XML reader for the multistatus
// replies of PROPFIND and REPORT. The replies are flat enough that a tag
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

static const char DAV_BODY_CAL_HOME[] =
  "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
  "<d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
  "<d:prop><c:calendar-home-set/></d:prop></d:propfind>";

static const char DAV_BODY_CAL_LIST[] =
  "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
  "<d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\" "
  "xmlns:a=\"http://apple.com/ns/ical/\" xmlns:cs=\"http://calendarserver.org/ns/\">"
  "<d:prop><d:resourcetype/><d:displayname/><a:calendar-color/><cs:getctag/><d:sync-token/>"
  "<c:supported-calendar-component-set/></d:prop></d:propfind>";

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
      if (!strcmp(name, "response")) { memset(&resp, 0, sizeof(resp)); psStatus = 0; }
      else if (!strcmp(name, "propstat")) { memset(&ps, 0, sizeof(ps)); psStatus = 0; }
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
          } else if (!strcmp(name, "getetag")) davCopy(ps.etag, sizeof(ps.etag), text);
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
