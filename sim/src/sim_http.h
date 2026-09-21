#ifndef SIM_HTTP_H_
#define SIM_HTTP_H_
#ifdef __cplusplus
extern "C" {
#endif
// Starts the host HTTP server on 127.0.0.1:<port>, serving the web portal
// from html_path and dispatching /rest/* to the firmware's own handlers.
void sim_http_start(int port, const char *html_path);
int sim_http_route_count(void);
#ifdef __cplusplus
}
#endif
#endif
