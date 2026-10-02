#include <stdio.h>
#include "frida-gum.h"

static void on_enter(
    GumInvocationContext *ic,
    gpointer user_data
)
{
    printf("CalculateMoney called\n");

    int a = GPOINTER_TO_INT(
        gum_invocation_context_get_nth_argument(ic, 0)
    );

    int b = GPOINTER_TO_INT(
        gum_invocation_context_get_nth_argument(ic, 1)
    );

    printf("a = %d, b = %d\n", a, b);
}


static void on_leave(
    GumInvocationContext *ic,
    gpointer user_data
)
{
    int old_result = GPOINTER_TO_INT(
        gum_invocation_context_get_return_value(ic)
    );

    printf("old result = %d\n", old_result);

    gum_invocation_context_replace_return_value(
        ic,
        GINT_TO_POINTER(999)
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
            (gpointer)0x400476,
            listener,
            NULL
        );

    printf("attach result = %d\n", result);
}