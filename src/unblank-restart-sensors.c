/* ------------------------------------------------------------------------- *
 * Copyright (C) 2014 Jolla Ltd.
 * Contact: Simo Piiroinen <simo.piiroinen@jollamobile.com>
 *          Simonas Leleiva <simonas.leleiva@jollamobile.com>
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of Google Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ------------------------------------------------------------------------- */

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <mce/dbus-names.h>
#include <mce/mode-names.h>

#define DBUS_NAME_OWNER_CHANGED_SIG "NameOwnerChanged"

/* ========================================================================= *
 * LOGGING
 * ========================================================================= */

#define ENABLE_DEBUG_LOGGING 0

#if ENABLE_DEBUG_LOGGING
static void monotime(struct timeval *tv)
{
    struct timespec ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    TIMESPEC_TO_TIMEVAL(tv, &ts);
}
#endif

static const char *log_level_repr(int lev)
{
    const char *res = "?";
    switch( lev ) {
    case LOG_EMERG:   res = "X"; break;
    case LOG_ALERT:   res = "A"; break;
    case LOG_CRIT:    res = "C"; break;
    case LOG_ERR:     res = "E"; break;
    case LOG_WARNING: res = "W"; break;
    case LOG_NOTICE:  res = "N"; break;
    case LOG_INFO:    res = "I"; break;
    case LOG_DEBUG:   res = "D"; break;
    default: break;
    }
    return res;
}

static void log_emit(int lev, const char *fmt, ...)
{
    va_list va;
    char *msg = 0;

    va_start(va, fmt);
    if( vasprintf(&msg, fmt, va) < 0 )
        msg = 0;
    va_end(va);

#if ENABLE_DEBUG_LOGGING
    static struct timeval t0, t1;
    struct timeval t2, d0, d1;

    monotime(&t2);
    if( !timerisset(&t0) )
        t0 = t1 = t0;

    timersub(&t2, &t1, &d1);
    timersub(&t2, &t0, &d0);

    if( d1.tv_sec >= 5 )
        t0 = t1 = t2;
    else
        t1 = t2;

    fprintf(stderr, "%ld.%03ld %ld.%03ld %s: %s\n",
            (long)d0.tv_sec, (long)(d0.tv_usec / 1000),
            (long)d1.tv_sec, (long)(d1.tv_usec / 1000),
            log_level_repr(lev), msg ?: fmt);
#else
    fprintf(stderr, "%s: %s\n", log_level_repr(lev), msg ?: fmt);
#endif

    free(msg);
}

#define log_error(  FMT,ARG...) log_emit(LOG_ERR,     FMT, ## ARG)
#define log_warning(FMT,ARG...) log_emit(LOG_WARNING, FMT, ## ARG)

#if ENABLE_DEBUG_LOGGING
# define log_notice( FMT,ARG...) log_emit(LOG_NOTICE,  FMT, ## ARG)
# define log_info(   FMT,ARG...) log_emit(LOG_INFO,    FMT, ## ARG)
# define log_debug(  FMT,ARG...) log_emit(LOG_DEBUG,   FMT, ## ARG)
#else
# define log_notice( FMT,ARG...) do{}while(0)
# define log_info(   FMT,ARG...) do{}while(0)
# define log_debug(  FMT,ARG...) do{}while(0)
#endif

/* ========================================================================= *
 * MAINLOOP
 * ========================================================================= */

static GMainLoop *mainloop_hnd = 0;
static int        mainloop_res = EXIT_SUCCESS;

static void mainloop_stop(int res)
{
    if( mainloop_res < res )
        mainloop_res = res;

    if( !mainloop_hnd )
        exit(EXIT_FAILURE);

    g_main_loop_quit(mainloop_hnd);
}

static int mainloop_run(void)
{
    mainloop_hnd = g_main_loop_new(0, 0);
    log_debug("enter mainloop");
    g_main_loop_run(mainloop_hnd);
    log_debug("leave mainloop");
    g_main_loop_unref(mainloop_hnd), mainloop_hnd = 0;

    return mainloop_res;
}

/* ========================================================================= *
 * DBUS GLUE
 * ========================================================================= */

static const char mce_display_ind_rule[] =
    "type='signal'"
",sender='"MCE_SERVICE"'"
",path='"MCE_SIGNAL_PATH"'"
",interface='"MCE_SIGNAL_IF"'"
",member='"MCE_DISPLAY_SIG"'"
;

static DBusConnection *xdbus_con = 0;
static bool xdbus_async_call_va(const char *srv,
                                const char *obj,
                                const char *ifc,
                                const char *fun,
                                DBusPendingCallNotifyFunction cb,
                                void *aptr,
                                DBusFreeFunction free_cb,
                                int type, va_list va)
{
    bool             res = false;
    DBusMessage     *req = 0;
    DBusPendingCall *pc  = 0;

    if( !xdbus_con )
        goto EXIT;

    if( !(req = dbus_message_new_method_call(srv, obj, ifc, fun)) )
        goto EXIT;

    if( !dbus_message_append_args_valist(req, type, va) )
        goto EXIT;

    if( !cb ) {
        dbus_message_set_no_reply(req, true);
        if( !dbus_connection_send(xdbus_con, req, 0) )
            goto EXIT;
    }
    else {
        if( !dbus_connection_send_with_reply(xdbus_con, req, &pc, -1) )
            goto EXIT;

        if( !dbus_pending_call_set_notify(pc, cb, aptr, free_cb) )
            goto EXIT;
    }

    res = true;

EXIT:
    if( !res )
        log_error("%s.%s: failed to initiate query", ifc, fun);

    if( pc )  dbus_pending_call_unref(pc);
    if( req ) dbus_message_unref(req);

    return res;
}

static bool xdbus_async_call(const char *srv,
                             const char *obj,
                             const char *ifc,
                             const char *fun,
                             DBusPendingCallNotifyFunction cb,
                             void *aptr,
                             DBusFreeFunction free_cb,
                             int type, ...)
{
    bool res;
    va_list va;
    va_start(va, type);
    res = xdbus_async_call_va(srv, obj, ifc, fun, cb, aptr, free_cb, type, va);
    va_end(va);
    return res;
}

static void xsystemd_restart_unit_cb(DBusPendingCall *pc, void *aptr)
{
}

static bool sensors_restart_in_progress = false;
static void xsystemd_restart_unit_free_cb(void *userdata)
{
    sensors_restart_in_progress = false;
}

static void xdbus_handle_display_state_signal(DBusMessage *msg)
{
    const char *name = 0;
    const char *unit = "sensorfwd.service";
    const char *mode = "replace";
    
    DBusError   err  = DBUS_ERROR_INIT;

    if( !dbus_message_get_args(msg, &err,
                               DBUS_TYPE_STRING, &name,
                               DBUS_TYPE_INVALID) )
    {
        log_warning("%s: %s", err.name, err.message);
        goto EXIT;
    }

    if (!strcmp(name, "on")) {
        bool res;

        if (sensors_restart_in_progress)
            goto EXIT;

        res = xdbus_async_call("org.freedesktop.systemd1",
                         "/org/freedesktop/systemd1",
                         "org.freedesktop.systemd1.Manager",
                         "RestartUnit",
                         xsystemd_restart_unit_cb, 0,
                         xsystemd_restart_unit_free_cb,
                         DBUS_TYPE_STRING, &unit,
                         DBUS_TYPE_STRING, &mode,
                         DBUS_TYPE_INVALID);

        if (res)
            sensors_restart_in_progress = true;
    }

EXIT:
    dbus_error_free(&err);
    return;
}

static DBusHandlerResult
xdbus_filter_cb(DBusConnection *con,
                DBusMessage *msg,
                void *aptr)
{
    (void)con; (void)aptr;

    DBusHandlerResult res = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if( dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_SIGNAL )
        goto EXIT;

    const char *interface = dbus_message_get_interface(msg);
    if( !interface )
        goto EXIT;

    const char *member = dbus_message_get_member(msg);
    if( !member )
        goto EXIT;

    if( !strcmp(interface, MCE_SIGNAL_IF) )
    {
        if( !strcmp(member, MCE_DISPLAY_SIG) )
            xdbus_handle_display_state_signal(msg);
    }

EXIT:
    return res;
}

static DBusConnection *
xdbus_init(void)
{
    DBusError err = DBUS_ERROR_INIT;
    DBusBusType bus_type = DBUS_BUS_SYSTEM;

    if( xdbus_con )
        goto EXIT;

    if( !(xdbus_con = dbus_bus_get(bus_type, &err)) ) {
        log_error("Failed to open connection to message bus"
                  "; %s: %s", err.name, err.message);
        goto EXIT;
    }

    dbus_connection_setup_with_g_main(xdbus_con, 0);

    dbus_connection_add_filter(xdbus_con, xdbus_filter_cb, 0, 0);

    dbus_bus_add_match(xdbus_con, mce_display_ind_rule, 0);

    log_debug("connected to system bus");

EXIT:
    dbus_error_free(&err);
    return xdbus_con;
}

static void
xdbus_exit(void)
{
    if( xdbus_con ) {
        dbus_connection_unref(xdbus_con), xdbus_con = 0;
        log_debug("disconnected from system bus");
    }
}

/* ========================================================================= *
 * SIGNAL HANDLER
 * ========================================================================= */

static const int sighnd_trap[] =
{
    SIGHUP,
    SIGINT,
    SIGQUIT,
    SIGTERM,
    -1
};

static int sighnd_pipe[2] = {-1, -1};

static void sighnd_handle_signal(const gint signr)
{
    switch (signr) {
    case SIGHUP:
    case SIGINT:
    case SIGQUIT:
        mainloop_stop(EXIT_FAILURE);
        break;

    case SIGTERM:
        mainloop_stop(EXIT_SUCCESS);
        break;

    default:
        break;
    }
}

static gboolean sighnd_rx_signal_cb(GIOChannel *chn, GIOCondition cnd,
                                    gpointer aptr)
{
    (void)chn; (void)cnd; (void)aptr;

    int sig = 0;
    int got = TEMP_FAILURE_RETRY(read(sighnd_pipe[0], &sig, sizeof sig));

    if( got != sizeof sig )
        abort();

    sighnd_handle_signal(sig);

    return TRUE;
}

static void sighnd_tx_signal_cb(int sig)
{
    /* NOTE: this function must be kept async-signal-safe! */
    static volatile int exit_tries = 0;

    static const char msg[] = "\n*** BREAK ***\n";

    /* FIXME: switch to sigaction() ... */
    signal(sig, sighnd_tx_signal_cb);

    switch( sig )
    {
    case SIGHUP:
    case SIGINT:
    case SIGQUIT:
    case SIGTERM:
        /* Message to stderr, do not care if it fails */
        if( write(STDERR_FILENO, msg, sizeof msg - 1) < 0 ) {
            // nop
        }

        /* If we get here multiple times, assume hung mainloop and exit */
        if( ++exit_tries >= 2 )
            abort();

        break;

    default:
        break;
    }

    /* transfer the signal to mainloop via pipe */
    int did = TEMP_FAILURE_RETRY(write(sighnd_pipe[1], &sig, sizeof sig));

    if( did != (int)sizeof sig )
        abort();
}

static bool sighnd_init(void)
{
    bool        res = false;
    GIOChannel *chn = 0;

    if( pipe(sighnd_pipe) == -1 )
        goto EXIT;

    if( (chn = g_io_channel_unix_new(sighnd_pipe[0])) == 0 )
        goto EXIT;

    if( !g_io_add_watch(chn, G_IO_IN, sighnd_rx_signal_cb, 0) )
        goto EXIT;

    for( size_t i = 0; sighnd_trap[i] != -1; ++i )
        signal(sighnd_trap[i], sighnd_tx_signal_cb);

    res = true;

EXIT:
    if( chn != 0 ) g_io_channel_unref(chn);

    return res;
}

/* ========================================================================= *
 * ENTRY POINT
 * ========================================================================= */

int
main()
{
    int xc = EXIT_FAILURE;

    printf("Starting service to restart sensors when display unblanks\n");

    if( !sighnd_init() )
        goto EXIT;

    if( !xdbus_init() )
        goto EXIT;

    xc = mainloop_run();

EXIT:
    xdbus_exit();
    return xc;
}
