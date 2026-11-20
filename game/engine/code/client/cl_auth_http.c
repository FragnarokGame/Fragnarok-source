#include "client.h"
#include "cl_auth.h"

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>

int CL_AuthRequest( const char *host, const char *key, const char *path,
        const char *body, const char *bearer, char *response, size_t capacity ) {
    HINTERNET session = NULL, connection = NULL, handle = NULL;
    WCHAR wideHost[128], widePath[128], headers[11000];
    char headerText[11000];
    DWORD code = 0, size = sizeof( code ), count, redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    size_t used = 0;
    int result = 0;

    response[0] = 0;
    if ( !MultiByteToWideChar( CP_UTF8, 0, host, -1, wideHost, ARRAY_LEN( wideHost ) ) ||
        !MultiByteToWideChar( CP_UTF8, 0, path, -1, widePath, ARRAY_LEN( widePath ) ) ) return 0;
    snprintf( headerText, sizeof( headerText ), "Content-Type: application/json\r\napikey: %s\r\n%s%s%s",
        key, bearer[0] ? "Authorization: Bearer " : "", bearer, bearer[0] ? "\r\n" : "" );
    if ( !MultiByteToWideChar( CP_UTF8, 0, headerText, -1, headers, ARRAY_LEN( headers ) ) ) goto cleanup;
    session = WinHttpOpen( L"FRAGNAROK", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
    if ( !session || !WinHttpSetTimeouts( session, 5000, 5000, 10000, 10000 ) ) goto cleanup;
    connection = WinHttpConnect( session, wideHost, INTERNET_DEFAULT_HTTPS_PORT, 0 );
    if ( !connection ) goto cleanup;
    handle = WinHttpOpenRequest( connection, L"POST", widePath, NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE );
    if ( !handle || !WinHttpSetOption( handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof( redirects ) ) ) goto cleanup;
    if ( !WinHttpSendRequest( handle, headers, (DWORD)-1, (void *)body, (DWORD)strlen( body ), (DWORD)strlen( body ), 0 ) ||
        !WinHttpReceiveResponse( handle, NULL ) ||
        !WinHttpQueryHeaders( handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX ) ) goto cleanup;
    for ( ;; ) {
        if ( used == capacity - 1 ) goto cleanup;
        if ( !WinHttpReadData( handle, response + used, (DWORD)( capacity - used - 1 ), &count ) ) goto cleanup;
        if ( !count ) break;
        used += count;
        response[used] = 0;
    }
    result = (int)code;
cleanup:
    SecureZeroMemory( headerText, sizeof( headerText ) );
    SecureZeroMemory( headers, sizeof( headers ) );
    if ( handle ) WinHttpCloseHandle( handle );
    if ( connection ) WinHttpCloseHandle( connection );
    if ( session ) WinHttpCloseHandle( session );
    return result;
}

#else
#include <curl/curl.h>
#include <SDL.h>

typedef struct {
    char *data;
    size_t used, capacity;
} authResponse_t;

static size_t Auth_Receive( char *data, size_t size, size_t count, void *context ) {
    authResponse_t *response = context;
    size_t bytes = size * count;
    if ( bytes >= response->capacity - response->used ) return 0;
    memcpy( response->data + response->used, data, bytes );
    response->used += bytes;
    response->data[response->used] = 0;
    return bytes;
}

int CL_AuthRequest( const char *host, const char *key, const char *path,
        const char *body, const char *bearer, char *response, size_t capacity ) {
    CURL *handle;
    struct curl_slist *headers = NULL, *next;
    char url[256], apiKey[2100], authorization[8250];
    long code = 0;
    authResponse_t buffer = { response, 0, capacity };
    CURLcode result;

    response[0] = 0;
    static SDL_SpinLock initLock;
    static qboolean initialized;
    qboolean ready;

    SDL_AtomicLock( &initLock );
    if ( !initialized ) initialized = curl_global_init( CURL_GLOBAL_DEFAULT ) == CURLE_OK;
    ready = initialized;
    SDL_AtomicUnlock( &initLock );
    if ( !ready ) return 0;
    handle = curl_easy_init();
    if ( !handle ) return 0;
    snprintf( url, sizeof( url ), "https://%s%s", host, path );
    snprintf( apiKey, sizeof( apiKey ), "apikey: %s", key );
    headers = curl_slist_append( NULL, "Content-Type: application/json" );
    if ( !headers ) goto cleanup;
    next = curl_slist_append( headers, apiKey );
    if ( !next ) goto cleanup;
    headers = next;
    if ( bearer[0] ) {
        snprintf( authorization, sizeof( authorization ), "Authorization: Bearer %s", bearer );
        next = curl_slist_append( headers, authorization );
        if ( !next ) goto cleanup;
        headers = next;
    }
    curl_easy_setopt( handle, CURLOPT_URL, url );
    curl_easy_setopt( handle, CURLOPT_HTTPHEADER, headers );
    curl_easy_setopt( handle, CURLOPT_POSTFIELDS, body );
    curl_easy_setopt( handle, CURLOPT_WRITEFUNCTION, Auth_Receive );
    curl_easy_setopt( handle, CURLOPT_WRITEDATA, &buffer );
    curl_easy_setopt( handle, CURLOPT_NOSIGNAL, 1L );
    curl_easy_setopt( handle, CURLOPT_CONNECTTIMEOUT, 5L );
    curl_easy_setopt( handle, CURLOPT_TIMEOUT, 20L );
    curl_easy_setopt( handle, CURLOPT_SSL_VERIFYPEER, 1L );
    curl_easy_setopt( handle, CURLOPT_SSL_VERIFYHOST, 2L );
    curl_easy_setopt( handle, CURLOPT_FOLLOWLOCATION, 0L );
    result = curl_easy_perform( handle );
    if ( result == CURLE_OK ) curl_easy_getinfo( handle, CURLINFO_RESPONSE_CODE, &code );
cleanup:
    {
        volatile unsigned char *p = (volatile unsigned char *)authorization;
        size_t remaining = sizeof( authorization );
        while ( remaining-- ) *p++ = 0;
    }
    curl_slist_free_all( headers );
    curl_easy_cleanup( handle );
    return (int)code;
}
#endif
