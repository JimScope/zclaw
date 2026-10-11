#ifndef DASH_URL_H
#define DASH_URL_H

#include <stdbool.h>
#include <stddef.h>

// "<base>/<name>.jpg". For "clima", appends "?lat=..&lon=.." when the saved
// location memory (e.g. "lat20.02lon-75.83") parses. name must be [a-z]+.
// Returns false on a bad name or when the URL does not fit.
bool dash_url_build(char *buf, size_t len, const char *base, const char *name,
                    const char *location);

#endif // DASH_URL_H
