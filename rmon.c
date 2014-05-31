/* Copyright (c) 2014, John Vogel
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *    
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* rmon.c -- track randr screen, crtc, output, and propety changes
 * portions based on randr code for xev
 */

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/randr.h>

xcb_connection_t *con;
xcb_screen_t *scr;
xcb_window_t w;
int quit = 0;

static const char *randr_screen_change_strings[] = {
    "XCB_RANDR_NOTIFY_CRTC_CHANGE",
    "XCB_RANDR_NOTIFY_OUTPUT_CHANGE",
    "XCB_RANDR_NOTIFY_OUTPUT_PROPERTY",
    "XCB_RANDR_NOTIFY_PROVIDER_CHANGE",
    "XCB_RANDR_NOTIFY_PROVIDER_PROPERTY",
    "XCB_RANDR_NOTIFY_RESOURCE_CHANGE",
};

static const char *randr_rotation_strings[] = {
    [1] = "XCB_RANDR_ROTATION_0",
    [2] = "XCB_RANDR_ROTATION_90",
    [4] = "XCB_RANDR_ROTATION_180",
    [8] = "XCB_RANDR_ROTATION_270",
    [16] = "XCB_RANDR_ROTATION_REFLECT_X",
    [32] = "XCB_RANDR_ROTATION_REFLECT_Y",
};

static const char *randr_connection_strings[] = {
    "XCB_RANDR_CONNECTION_CONNECTED",
    "XCB_RANDR_CONNECTION_DISCONNECTED",
    "XCB_RANDR_CONNECTION_UNKOWN",
};

static const char *propstatus_strings[] = { "NEW", "DELETE", };

void handle_randr_screen_change(xcb_generic_event_t *gev)
{
    xcb_randr_screen_change_notify_event_t *ev =
        (xcb_randr_screen_change_notify_event_t *)gev;

    printf("randr_screen_change:\n");
    printf("\troot 0x%lx, timestamp %lu, config_timestamp %lu\n",
            ev->root, ev->timestamp, ev->config_timestamp);
    printf("\trotation %s\n", randr_rotation_strings[ev->rotation]);
    printf("\twidth %d, height %d, mwidth %d, mheight %d\n",
            ev->width, ev->height, ev->mwidth, ev->mheight);
}

void handle_randr_crtc_change(xcb_randr_notify_event_t *nev)
{
    xcb_randr_crtc_change_t *cc = &nev->u.cc;

    printf("\trandr_crtc_change:\n");
    printf("\twindow 0x%lx, timestamp %lu, crtc %lu, mode %lu\n",
            cc->window, cc->timestamp, cc->crtc, cc->mode);
    printf("\trotation %s\n", randr_rotation_strings[cc->rotation]);
    printf("\tx %d, y %d, width %d, height %d\n",
            cc->x, cc->y, cc->width, cc->height);
}

void handle_randr_output_change(xcb_randr_notify_event_t *nev)
{
    xcb_randr_output_change_t *oc = &nev->u.oc;

    printf("\trandr_output_change:\n");
    printf("\twindow 0x%lx, timestamp %lu, config_timestamp %lu\n",
            oc->window, oc->timestamp, oc->config_timestamp);
    printf("\toutput %lu, crtc %lu, mode %lu\n", oc->output, oc->crtc, oc->mode);
    printf("\trotation %s\n", randr_rotation_strings[oc->rotation]);
    printf("\tconnection %s\n", randr_connection_strings[oc->connection]);
    /* skip subpixel_order for now */
}

void handle_randr_output_property(xcb_randr_notify_event_t *nev)
{
    xcb_randr_output_property_t *op = &nev->u.op;
    xcb_get_atom_name_cookie_t gan_c;
    xcb_get_atom_name_reply_t *gan_r;
    char *propname;

    printf("\trandr_output_property:\n");
    printf("\twindow 0x%lx, timestamp %lu, output %lu\n",
            op->window, op->timestamp, op->output);

    gan_c = xcb_get_atom_name_unchecked(con, op->atom);
    gan_r = xcb_get_atom_name_reply(con, gan_c, NULL);
    if (!gan_r) {
        printf("\tfailed to get name for atom %lu\n", op->atom);
        return;
    }
    propname = xcb_get_atom_name_name(gan_r);

    printf("\tproperty %s (%s)\n", propname, propstatus_strings[op->status]);
    free(gan_r);
}

void handle_randr_notify(xcb_generic_event_t *gev)
{
    xcb_randr_notify_event_t *ev = (xcb_randr_notify_event_t *)gev;

    printf("randr_notify (subtype %d)\n", ev->subCode);

    if (ev->subCode == XCB_RANDR_NOTIFY_CRTC_CHANGE)
        handle_randr_crtc_change(ev);
    else if (ev->subCode == XCB_RANDR_NOTIFY_OUTPUT_CHANGE)
        handle_randr_output_change(ev);
    else if (ev->subCode == XCB_RANDR_NOTIFY_OUTPUT_PROPERTY)
        handle_randr_output_property(ev);
}

void termination_handler(int signum)
{
    quit++;
}

int main (int argc, char *argv[])
{
    int defscr;
    unsigned values[2];
    const xcb_query_extension_reply_t *qer;
    struct sigaction sa;

    con = xcb_connect(NULL, &defscr);
    if (xcb_connection_has_error(con)) {
        fprintf(stderr, "Failed to connect to display\n");
        exit(EXIT_FAILURE);
    }

    sa.sa_handler = termination_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    scr = xcb_setup_roots_iterator(xcb_get_setup(con)).data;
    if (!scr) {
        fprintf(stderr, "Failed to get default screen\n");
        xcb_disconnect(con);
        exit(EXIT_FAILURE);
    }

    qer = xcb_get_extension_data(con, &xcb_randr_id);
    if (!qer || !qer->present) {
        fprintf(stderr, "RandR extension missing\n");
        exit(EXIT_FAILURE);
    }

    values[0] = XCB_EVENT_MASK_EXPOSURE |
                XCB_EVENT_MASK_VISIBILITY_CHANGE |
                XCB_EVENT_MASK_STRUCTURE_NOTIFY |
                XCB_EVENT_MASK_RESIZE_REDIRECT |
                XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;
    w = xcb_generate_id(con);
    xcb_create_window(con, scr->root_depth, w, scr->root, 0, 0, 1, 1, 0,
            XCB_WINDOW_CLASS_COPY_FROM_PARENT, XCB_COPY_FROM_PARENT,
            XCB_CW_EVENT_MASK, values);
    xcb_randr_select_input(con, w, XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE |
                                    XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE |
                                    XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE |
                                    XCB_RANDR_NOTIFY_MASK_OUTPUT_PROPERTY);
    xcb_flush(con);

    while (!quit) {
        xcb_generic_event_t *ev;
        
        while ((ev = xcb_poll_for_event(con)) != NULL) {
            unsigned int rtype = ev->response_type;
            if (rtype  == (qer->first_event + XCB_RANDR_SCREEN_CHANGE_NOTIFY)) {
                handle_randr_screen_change(ev);
            }
            else if (rtype == qer->first_event + XCB_RANDR_NOTIFY)
                handle_randr_notify(ev);
            free(ev);
        }

        if (xcb_connection_has_error(con)) {
            fprintf(stderr, "Display connection closed by server\n");
            quit++;
        }
    }

    xcb_destroy_window(con, w);
    xcb_disconnect(con);
    return EXIT_SUCCESS;
}
