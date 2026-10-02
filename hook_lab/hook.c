#include <stdio.h>
#include "frida-gum.h"
// #include "game.h"

typedef struct
{
    int id;
    int owner_id;
    int ships;
    int morale;

} EU4_Fleet;

typedef struct
{
    int id;
    int treasury;
    int ships;
    int at_war;

} EU4_Country;

static void on_enter(
    GumInvocationContext *ic,
    gpointer user_data
)
{
    EU4_Country *country = (EU4_Country *)
        gum_invocation_context_get_nth_argument(ic, 0);

    EU4_Fleet *fleet = (EU4_Fleet *)
        gum_invocation_context_get_nth_argument(ic, 1);

    printf(
        "fleet=%d ships=%d owner_id=%d treasury=%d\n",
        fleet->id,
        fleet->ships,
        fleet->owner_id,
        country->treasury
    );
}

static void on_leave(
    GumInvocationContext *ic,
    gpointer user_data
)
{
    gum_invocation_context_replace_return_value(
        ic,
        GINT_TO_POINTER(1)
    );
}

__attribute__((constructor))
static void init(void)
{
    gum_init_embedded();

    GumInterceptor *interceptor =
        gum_interceptor_obtain();

    GumInvocationListener *listener =
        gum_make_call_listener(
            on_enter,
            on_leave,
            NULL,
            NULL
        );

    GumAttachReturn result =
        gum_interceptor_attach(
            interceptor,
            (gpointer)0x4004d7,
            listener,
            NULL
        );

    printf("attach result = %d\n", result);
}