#include <frida-gum.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/*
 * EU4 Linux x86-64 - Naval Condottieri integrated prototype
 *
 * To Do:
 *   - mechanizm break condottieri
 *   - rozbicie tego monolitu na sensowne pliki
 */

/* ========================================================= */
/* CONSTANTS                                               */
/* ========================================================= */

#define MAX_CONTRACTS 128
#define MAX_NAVY_UI_ENTRIES 128

/* ========================================================= */
/* OPAQUE TYPES                                              */
/* ========================================================= */

typedef void CUnit;
typedef void CNavy;
typedef void CCountry;
typedef void CArmyItem;
typedef void COfferCondottieriDiplomacyDialog;
typedef void CCountryDiplomacyView;
typedef uint64_t CCountryTag;

/* ========================================================= */
/* BUILD-SPECIFIC ADDRESSES                                  */
/* ========================================================= */

#define ADDR_MAY_FIGHT                                 0x00000000018fee80ULL
#define ADDR_WAR_HAS_ENTRY                             0x0000000000db8724ULL
#define ADDR_BUILDUNITLIST_ISARMY_JE                   0x0000000001dd6604ULL
#define ADDR_CARMYITEM_UPDATE                          0x0000000001dd7a7cULL
#define ADDR_OFFER_DIALOG_CTOR                         0x0000000001dd5368ULL
#define ADDR_OFFER_DIALOG_ONACCEPT                     0x0000000001dd6a4aULL
#define ADDR_BUTTON_NOTIFY_CLICKED                     0x0000000002058d7cULL
#define ADDR_GUIOBJECT_GET_NAME                        0x0000000001b4b878ULL
#define ADDR_INSTANTTEXT_CHANGE_CSTRING                0x000000000208b442ULL
#define ADDR_CCOUNTRYDIPLOMACYVIEW_SHOWMAINWINDOW      0x0000000001a71dd4ULL
#define ADDR_COUNTRY_DATABASE_PTR                      0x0000000002fc52f8ULL

/* ========================================================= */
/* OFFSETS                                                   */
/* ========================================================= */

#define OFFSET_UNIT_COUNTRY_TAG                        0x0c8
#define OFFSET_COUNTRY_WARS                            0x1530
#define OFFSET_CARMYITEM_UNIT                          0x0b8
#define OFFSET_CARMYITEM_INFO_TEXT                     0x130
#define OFFSET_CARMYITEM_CHECKBOX                      0x138
#define VTABLE_SLOT_IS_NAVY                            0x0a0

/*
 * CCountryDiplomacyView - observed semantics for this Offer Condottieri path:
 *
 *   +0x208 = diplomacy counterpart / recipient of the offer
 *   +0x218 = current player / fleet owner
 *
 * This was confirmed at runtime because BuildUnitList enumerates the player's
 * units while a dialog showed e.g. +0x208=MKL and +0x218=DAN, and the listed
 * navy's owner resolved to DAN.
 */

#define OFFSET_DIPLO_VIEW_COUNTERPART_TAG              0x208
#define OFFSET_DIPLO_VIEW_PLAYER_TAG                   0x218

/*
 * CCountryDataBase:
 *   +0xf8 -> CCountry ** by country index
 */

#define OFFSET_COUNTRY_DB_COUNTRY_ARRAY                0x0f8

/* ========================================================= */
/* LOG                                                       */
/* ========================================================= */

static FILE *g_log_file = NULL;

static void navlog_clear()
{
    g_log_file = fopen("/home/piotrrusak/naval-condottieri-eu4", "w");
}

static void navlog(const char *fmt, ...)
{
    va_list ap;

    if (g_log_file == NULL)
        g_log_file = fopen("/home/piotrrusak/naval-condottieri-eu4", "a");

    va_start(ap, fmt);

    if (g_log_file != NULL) {
        va_list copy;
        va_copy(copy, ap);
        fprintf(g_log_file, "[nav-condottieri] ");
        vfprintf(g_log_file, fmt, copy);
        fprintf(g_log_file, "\n");
        fflush(g_log_file);
        va_end(copy);
    }

    fprintf(stderr, "[nav-condottieri] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    fflush(stderr);

    va_end(ap);
}


typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} CStringPartial;

typedef CStringPartial *(*CGuiObjectGetNameFn)(void *self);

static CGuiObjectGetNameFn g_gui_get_name = (CGuiObjectGetNameFn) (uintptr_t) ADDR_GUIOBJECT_GET_NAME;

static const char *gui_object_name(void *obj)
{
    if (obj == NULL || g_gui_get_name == NULL)
        return NULL;

    CStringPartial *s = g_gui_get_name(obj);
    if (s == NULL || s->data == NULL)
        return NULL;

    return s->data;
}

/* ========================================================= */
/* TAG HELPERS                                               */
/* ========================================================= */

static CCountryTag get_unit_country_tag(CUnit *unit)
{
    CCountryTag tag = 0;

    if (unit == NULL)
        return 0;

    memcpy(&tag, (uint8_t *) unit + OFFSET_UNIT_COUNTRY_TAG, sizeof(tag));

    return tag;
}

static CCountryTag read_tag_at(void *base, size_t offset)
{
    CCountryTag tag = 0;

    if (base == NULL)
        return 0;

    memcpy(&tag, (uint8_t *) base + offset, sizeof(tag));
    return tag;
}

static uint16_t tag_get_index(CCountryTag tag)
{
    uint16_t index = 0;
    uint8_t bytes[8];

    memcpy(bytes, &tag, sizeof(bytes));
    memcpy(&index, bytes + 4, sizeof(index));

    return index;
}

static void tag_to_string(CCountryTag tag, char out[4])
{
    uint8_t bytes[8];

    memcpy(bytes, &tag, sizeof(bytes));

    out[0] = (char) bytes[0];
    out[1] = (char) bytes[1];
    out[2] = (char) bytes[2];
    out[3] = '\0';
}

/* ========================================================= */
/* COUNTRY LOOKUP                                            */
/* ========================================================= */

static CCountry *country_from_tag(CCountryTag tag)
{
    uint16_t index = tag_get_index(tag);

    void *db = *(void **) (uintptr_t) ADDR_COUNTRY_DATABASE_PTR;
    if (db == NULL) {
        navlog("country_from_tag: db == NULL");
        return NULL;
    }

    CCountry **countries = *(CCountry ***) ((uint8_t *) db + OFFSET_COUNTRY_DB_COUNTRY_ARRAY);

    if (countries == NULL) {
        navlog("country_from_tag: countries == NULL");
        return NULL;
    }

    return countries[index];
}

/* ========================================================= */
/* CONTRACT REGISTRY                                         */
/* ========================================================= */

typedef struct {
    CNavy *navy;
    CCountry *contractor;
    CCountry *contracter;
    int cost;
} NavalCondottieriContract;

static NavalCondottieriContract g_contracts[MAX_CONTRACTS];
static int g_contract_count = 0;

static NavalCondottieriContract *find_contract(void *navy)
{
    if (navy == NULL)
        return NULL;

    for (int i = 0; i < g_contract_count; i++) {
        if (g_contracts[i].navy == navy)
            return &g_contracts[i];
    }

    return NULL;
}

static bool add_contract(CNavy *navy,
             CCountry *contractor,
             CCountry *contracter,
             int cost)
{
    if (navy == NULL) {
        navlog("add_contract: navy == NULL");
        return false;
    }

    if (contracter == NULL) {
        navlog("add_contract: renter == NULL");
        return false;
    }

    if (g_contract_count >= MAX_CONTRACTS) {
        navlog("add_contract: registry full");
        return false;
    }

    if (find_contract(navy) != NULL) {
        navlog("add_contract: navy already registered");
        return false;
    }

    NavalCondottieriContract *c = &g_contracts[g_contract_count];
    memset(c, 0, sizeof(*c));

    c->navy = navy;
    c->contractor = contractor;
    c->contracter = contracter;
    c->cost = cost;

    g_contract_count++;

    navlog(
        "CONTRACT ADDED navy=%p owner=%p renter=%p cost=%d count=%d",
        navy, contractor, contracter, cost, g_contract_count
    );

    return true;
}

static bool remove_contract(CNavy *navy)
{
    for (int i = 0; i < g_contract_count; i++) {
        if (g_contracts[i].navy != navy)
            continue;

        g_contracts[i] = g_contracts[g_contract_count - 1];
        memset(&g_contracts[g_contract_count - 1],
               0,
               sizeof(g_contracts[0]));

        g_contract_count--;

        navlog(
            "CONTRACT REMOVED navy=%p count=%d",
            navy, g_contract_count
        );

        return true;
    }

    return false;
}

/* ========================================================= */
/* EXPORTED TEST API                                         */
/* ========================================================= */

__attribute__((visibility("default"))) int nvct_contract_count(void)
{
    return g_contract_count;
}

__attribute__((visibility("default"))) void nvct_contract_clear(void)
{
    g_contract_count = 0;
}

/* ========================================================= */
/* WAR CHECK                                                 */
/* ========================================================= */

typedef bool (*HasWarEntryFn)(void *self, CCountryTag target);

static HasWarEntryFn g_has_war_entry = (HasWarEntryFn) (uintptr_t) ADDR_WAR_HAS_ENTRY;

static bool country_at_war_with(CCountry *country, CCountryTag target)
{
    if (country == NULL)
        return false;

    void *wars = (void *) ((uint8_t *) country + OFFSET_COUNTRY_WARS);

    return g_has_war_entry(wars, target);
}

static bool contract_may_fight(CUnit *unit_a, CUnit *unit_b, NavalCondottieriContract *contract_a, NavalCondottieriContract *contract_b)
{
    CCountryTag tag_a = get_unit_country_tag(unit_a);
    CCountryTag tag_b = get_unit_country_tag(unit_b);

    if (contract_a != NULL) {
        if (country_at_war_with(contract_a->contracter, tag_b))
            return true;
    }

    if (contract_b != NULL) {
        if (country_at_war_with(contract_b->contracter, tag_a))
            return true;
    }

    return false;
}

/* ========================================================= */
/* IS NAVY                                                   */
/* ========================================================= */

typedef uint8_t (*CUnitIsNavyVirtualFn)(CUnit *unit);

static gboolean unit_is_navy(CUnit *unit)
{
    if (unit == NULL)
        return FALSE;

    void **vtable = *(void ***) unit;
    if (vtable == NULL)
        return FALSE;

    CUnitIsNavyVirtualFn fn = (CUnitIsNavyVirtualFn) *(void **) ((uint8_t *) vtable + VTABLE_SLOT_IS_NAVY);

    if (fn == NULL)
        return FALSE;

    return fn(unit) != 0;
}

/* ========================================================= */
/* UI NAVY ENTRY TRACKING                                    */
/* ========================================================= */

typedef struct {
    CArmyItem *item;
    CNavy *navy;
    void *checkbox;
    bool selected;
} NavyUiEntry;

static NavyUiEntry g_navy_ui_entries[MAX_NAVY_UI_ENTRIES];
static int g_navy_ui_count = 0;

static COfferCondottieriDiplomacyDialog *g_active_dialog = NULL;
static CCountryDiplomacyView *g_active_view = NULL;

static NavyUiEntry *find_ui_entry_by_item(CArmyItem *item)
{
    for (int i = 0; i < g_navy_ui_count; i++) {
        if (g_navy_ui_entries[i].item == item)
            return &g_navy_ui_entries[i];
    }

    return NULL;
}

static NavyUiEntry *find_ui_entry_by_checkbox(void *checkbox)
{
    for (int i = 0; i < g_navy_ui_count; i++) {
        if (g_navy_ui_entries[i].checkbox == checkbox)
            return &g_navy_ui_entries[i];
    }

    return NULL;
}

static void reset_ui_entries(void)
{
    memset(g_navy_ui_entries, 0, sizeof(g_navy_ui_entries));
    g_navy_ui_count = 0;
}

static void register_navy_ui_entry(CArmyItem *item, CNavy *navy)
{
    if (item == NULL || navy == NULL)
        return;

    if (find_ui_entry_by_item(item) != NULL)
        return;

    if (g_navy_ui_count >= MAX_NAVY_UI_ENTRIES) {
        navlog("UI navy registry full");
        return;
    }

    void *checkbox = *(void **) ((uint8_t *) item + OFFSET_CARMYITEM_CHECKBOX);

    NavyUiEntry *e = &g_navy_ui_entries[g_navy_ui_count++];
    memset(e, 0, sizeof(*e));

    e->item = item;
    e->navy = navy;
    e->checkbox = checkbox;
    e->selected = false;

    navlog(
        "UI NAVY registered item=%p navy=%p checkbox=%p",
        item, navy, checkbox
    );
}

/* ========================================================= */
/* BUILDUNITLIST PATCH                                       */
/* ========================================================= */

static gboolean g_branch_patch_installed = FALSE;
static guint8 g_saved_branch[2];

static void write_branch_nops(gpointer mem, gpointer user_data)
{
    (void) user_data;
    ((guint8 *) mem)[0] = 0x90;
    ((guint8 *) mem)[1] = 0x90;
}

static void restore_branch_bytes(gpointer mem, gpointer user_data)
{
    (void) user_data;
    memcpy(mem, g_saved_branch, sizeof(g_saved_branch));
}

static gboolean install_buildunitlist_patch(void)
{
    guint8 *site = (guint8 *) (uintptr_t) ADDR_BUILDUNITLIST_ISARMY_JE;

    g_saved_branch[0] = site[0];
    g_saved_branch[1] = site[1];

    if (g_saved_branch[0] == 0x90 &&
        g_saved_branch[1] == 0x90) {

        navlog("BuildUnitList already patched (90 90)");
        g_branch_patch_installed = FALSE;
        return TRUE;
    }

    if (g_saved_branch[0] != 0x74 ||
        g_saved_branch[1] != 0x0e) {

        navlog(
            "ERROR BuildUnitList: got %02x %02x expected 74 0e or 90 90",
            g_saved_branch[0], g_saved_branch[1]
        );

        return FALSE;
    }

    gum_memory_patch_code(
        (gpointer) (uintptr_t) ADDR_BUILDUNITLIST_ISARMY_JE,
        2,
        write_branch_nops,
        NULL
    );

    if (site[0] != 0x90 || site[1] != 0x90) {
        navlog("ERROR BuildUnitList verification failed");
        return FALSE;
    }

    g_branch_patch_installed = TRUE;

    navlog("BuildUnitList patched 74 0e -> 90 90");
    return TRUE;
}

/* ========================================================= */
/* CArmyItem::Update REPLACEMENT                             */
/* ========================================================= */

typedef void (*CArmyItemUpdateFn)(CArmyItem *self);
typedef void *(*CUnitGetNameVirtualFn)(CUnit *unit);
typedef void (*CInstantTextChangeCStringFn)(void *self, void *text, bool unknown);

static GumInterceptor *g_interceptor = NULL;
static CArmyItemUpdateFn g_original_armyitem_update = NULL;
static gboolean g_update_hook_installed = FALSE;

static CInstantTextChangeCStringFn g_change_text_cstring =
    (CInstantTextChangeCStringFn) (uintptr_t) ADDR_INSTANTTEXT_CHANGE_CSTRING;

static void navy_item_set_name(CArmyItem *self, CUnit *unit)
{
    if (self == NULL || unit == NULL)
        return;

    void *info_text = *(void **) ((uint8_t *) self + OFFSET_CARMYITEM_INFO_TEXT);
    if (info_text == NULL)
        return;

    void **unit_vtable = *(void ***) unit;
    if (unit_vtable == NULL)
        return;

    CUnitGetNameVirtualFn get_name =
        (CUnitGetNameVirtualFn) *(void **) ((uint8_t *) unit_vtable + 0x48);

    if (get_name == NULL)
        return;

    void *name = get_name(unit);
    if (name != NULL)
        g_change_text_cstring(info_text, name, false);
}

static void armyitem_update_replacement(CArmyItem *self)
{
    if (self == NULL)
        return;

    CUnit *unit = *(CUnit **) ((uint8_t *) self + OFFSET_CARMYITEM_UNIT);

    gboolean is_navy = unit_is_navy(unit);

    if (is_navy) {
        register_navy_ui_entry(self, (CNavy *) unit);
        navy_item_set_name(self, unit);

        navlog(
            "CArmyItem::Update CNavy NAME-ONLY item=%p navy=%p",
            self, unit
        );
        return;
    }

    if (g_original_armyitem_update != NULL)
        g_original_armyitem_update(self);
}

static gboolean install_armyitem_update_hook(void)
{
    gpointer original = NULL;

    gum_interceptor_begin_transaction(g_interceptor);

    GumReplaceReturn result =
        gum_interceptor_replace(
            g_interceptor,
            (gpointer) (uintptr_t) ADDR_CARMYITEM_UPDATE,
            GUM_FUNCPTR_TO_POINTER(armyitem_update_replacement),
            &original,
            NULL
        );

    gum_interceptor_end_transaction(g_interceptor);

    if (result != GUM_REPLACE_OK) {
        navlog(
            "ERROR Update replace=%d",
            (int) result
        );
        return FALSE;
    }

    g_original_armyitem_update =
        (CArmyItemUpdateFn) original;

    g_update_hook_installed = TRUE;

    navlog(
        "CArmyItem::Update hooked @ 0x%llx original=%p",
        (unsigned long long) ADDR_CARMYITEM_UPDATE, original
    );

    return TRUE;
}

/* ========================================================= */
/* DIALOG CTOR LISTENER                                      */
/* ========================================================= */

static GumInvocationListener *g_ctor_listener = NULL;

static void offer_ctor_on_enter(GumInvocationContext *ic, gpointer user_data)
{
    (void) user_data;

    g_active_dialog =
        (COfferCondottieriDiplomacyDialog *)
        gum_invocation_context_get_nth_argument(ic, 0);

    g_active_view =
        (CCountryDiplomacyView *)
        gum_invocation_context_get_nth_argument(ic, 3);

    reset_ui_entries();

    CCountryTag counterpart_tag =
        read_tag_at(g_active_view, OFFSET_DIPLO_VIEW_COUNTERPART_TAG);

    CCountryTag player_tag =
        read_tag_at(g_active_view, OFFSET_DIPLO_VIEW_PLAYER_TAG);

    char counterpart_s[4];
    char player_s[4];

    tag_to_string(counterpart_tag, counterpart_s);
    tag_to_string(player_tag, player_s);

    navlog(
        "Offer dialog OPEN dialog=%p view=%p counterpart=%s(%u) player=%s(%u)",
        g_active_dialog, g_active_view, counterpart_s, (unsigned) tag_get_index(counterpart_tag), player_s, (unsigned) tag_get_index(player_tag)
    );
}

static gboolean install_ctor_listener(void)
{
    g_ctor_listener =
        gum_make_call_listener(
            offer_ctor_on_enter,
            NULL,
            NULL,
            NULL
        );

    if (g_ctor_listener == NULL)
        return FALSE;

    GumAttachReturn result =
        gum_interceptor_attach(
            g_interceptor,
            GSIZE_TO_POINTER(ADDR_OFFER_DIALOG_CTOR),
            g_ctor_listener,
            NULL
        );

    navlog(
        "Offer ctor listener result=%d",
        (int) result
    );

    return result == GUM_ATTACH_OK;
}

/* ========================================================= */
/* GLOBAL BUTTON CLICK LISTENER                              */
/* ========================================================= */

static GumInvocationListener *g_click_listener = NULL;

static void button_click_on_enter(GumInvocationContext *ic, gpointer user_data)
{
    (void) user_data;

    void *params = gum_invocation_context_get_nth_argument(ic, 1);

    if (params == NULL)
        return;

    void *clicked_object = *(void **) params;
    if (clicked_object == NULL)
        return;

    const char *name = gui_object_name(clicked_object);

    /*
     * Logujemy kliknięcia tylko gdy aktywny jest dialog Offer Condottieri.
     * Dzięki temu od razu widać, jaki dokładnie pointer daje NotifyClicked.
     */
    if (g_active_dialog != NULL) {
        navlog(
            "CLICK object=%p name=%s",
            clicked_object, name != NULL ? name : "<null>"
        );
    }
}

static gboolean install_click_listener(void)
{
    g_click_listener =
        gum_make_call_listener(
            button_click_on_enter,
            NULL,
            NULL,
            NULL
        );

    if (g_click_listener == NULL)
        return FALSE;

    GumAttachReturn result =
        gum_interceptor_attach(
            g_interceptor,
            GSIZE_TO_POINTER(ADDR_BUTTON_NOTIFY_CLICKED),
            g_click_listener,
            NULL
        );

    navlog(
        "NotifyClicked listener result=%d",
        (int) result
    );

    return result == GUM_ATTACH_OK;
}

/* ========================================================= */
/* OFFER / OnAccept                                          */
/* ========================================================= */

typedef void (*ShowMainWindowFn)(CCountryDiplomacyView *self);

static ShowMainWindowFn show_main_window = (ShowMainWindowFn) (uintptr_t) ADDR_CCOUNTRYDIPLOMACYVIEW_SHOWMAINWINDOW;

typedef void (*OfferOnAcceptFn)(COfferCondottieriDiplomacyDialog *self);

static OfferOnAcceptFn g_original_onaccept = NULL;
static gboolean g_onaccept_hook_installed = FALSE;

static int count_selected_navies(void)
{
    int n = 0;

    for (int i = 0; i < g_navy_ui_count; i++) {
        CArmyItem *item = g_navy_ui_entries[i].item;
        if (item == NULL)
            continue;
        g_navy_ui_entries[i].selected = *((uint8_t *)(g_navy_ui_entries[i].checkbox) + 0x100) != 0;
        
        if (g_navy_ui_entries[i].selected) {
            n++;
        }
    }

    return n;
}

static void offer_onaccept_replacement(
    COfferCondottieriDiplomacyDialog *self
)
{
    int selected = count_selected_navies();

    navlog(
        "OFFER clicked dialog=%p active=%p selected_navies=%d (should be max 1) visible_navies=%d",
        self, g_active_dialog, selected, g_navy_ui_count
    );
    
    if (self != g_active_dialog ||
        g_active_view == NULL ||
        g_navy_ui_count == 0 ||
        selected == 0
    ) {
        navlog("OFFER -> vanilla OnAccept");

        if (g_original_onaccept != NULL)
            g_original_onaccept(self);

        return;
    }

    CCountryTag renter_tag =
        read_tag_at(
            g_active_view,
            OFFSET_DIPLO_VIEW_COUNTERPART_TAG
        );

    CCountry *renter =
        country_from_tag(renter_tag);

    char renter_s[4];
    tag_to_string(renter_tag, renter_s);

    navlog(
        "NAVAL OFFER renter=%s index=%u country=%p",
        renter_s, (unsigned) tag_get_index(renter_tag), renter
    );

    int created = 0;

    for (int i = 0; i < g_navy_ui_count; i++) {
        NavyUiEntry *entry = &g_navy_ui_entries[i];

        if (!entry->selected)
            continue;

        CCountryTag owner_tag =
            get_unit_country_tag((CUnit *) entry->navy);

        CCountry *owner =
            country_from_tag(owner_tag);

        char owner_s[4];
        tag_to_string(owner_tag, owner_s);

        navlog(
            "NAVAL CONTRACT navy=%p owner=%s(%u) owner_country=%p renter=%s(%u) renter_country=%p",
            entry->navy, owner_s, (unsigned) tag_get_index(owner_tag), owner, renter_s, (unsigned) tag_get_index(renter_tag), renter
        );

        if (add_contract(entry->navy, owner, renter, 0)) {
            created++;
        }
    }

    navlog(
        "NAVAL OFFER finished created=%d (should be 1) total_contracts=%d",
        created, g_contract_count
    );

    reset_ui_entries();

    g_original_onaccept(self);
}

static gboolean install_onaccept_hook(void)
{
    gpointer original = NULL;

    gum_interceptor_begin_transaction(g_interceptor);

    GumReplaceReturn result =
        gum_interceptor_replace(
            g_interceptor,
            (gpointer) (uintptr_t) ADDR_OFFER_DIALOG_ONACCEPT,
            GUM_FUNCPTR_TO_POINTER(offer_onaccept_replacement),
            &original,
            NULL
        );

    gum_interceptor_end_transaction(g_interceptor);

    if (result != GUM_REPLACE_OK) {
        navlog(
            "ERROR OnAccept replace=%d",
            (int) result
        );
        return FALSE;
    }

    g_original_onaccept =
        (OfferOnAcceptFn) original;

    g_onaccept_hook_installed = TRUE;

    navlog(
        "Offer OnAccept hooked @ 0x%llx original=%p",
        (unsigned long long) ADDR_OFFER_DIALOG_ONACCEPT, original
    );

    return TRUE;
}

/* ========================================================= */
/* MAYFIGHT HOOK                                             */
/* ========================================================= */

typedef struct {
    bool override_return;
    bool return_value;
} MayFightCallState;

static GumInvocationListener *g_mayfight_listener = NULL;

static void mayfight_on_enter(GumInvocationContext *ic, gpointer user_data)
{
    (void) user_data;

    CUnit *unit_a = (CUnit *)gum_invocation_context_get_nth_argument(ic, 0);

    CUnit *unit_b = (CUnit *)gum_invocation_context_get_nth_argument(ic, 1);

    MayFightCallState *state = (MayFightCallState *)gum_invocation_context_get_listener_invocation_data(ic, sizeof(MayFightCallState));

    if (state == NULL)
        return;

    state->override_return = false;
    state->return_value = false;

    if (unit_a == NULL || unit_b == NULL)
        return;

    NavalCondottieriContract *contract_a =
        find_contract(unit_a);

    NavalCondottieriContract *contract_b =
        find_contract(unit_b);

    if (contract_a == NULL && contract_b == NULL) {
        return;
    }

    state->return_value =
        contract_may_fight(
            unit_a,
            unit_b,
            contract_a,
            contract_b
        );

    state->override_return = true;
}

static void
mayfight_on_leave(GumInvocationContext *ic, gpointer user_data)
{
    (void) user_data;

    MayFightCallState *state = (MayFightCallState *)gum_invocation_context_get_listener_invocation_data(ic, sizeof(MayFightCallState));

    if (state == NULL || !state->override_return) {
        return;
    }

    navlog(
        "MayFight override -> %d",
        state->return_value ? 1 : 0
    );

    gum_invocation_context_replace_return_value(
        ic,
        GSIZE_TO_POINTER(
            state->return_value ? 1 : 0
        )
    );
}

static gboolean install_mayfight_hook(void)
{
    g_mayfight_listener =
        gum_make_call_listener(
            mayfight_on_enter,
            mayfight_on_leave,
            NULL,
            NULL
        );

    if (g_mayfight_listener == NULL)
        return FALSE;

    GumAttachReturn result =
        gum_interceptor_attach(
            g_interceptor,
            GSIZE_TO_POINTER(ADDR_MAY_FIGHT),
            g_mayfight_listener,
            NULL
        );

    navlog(
        "MayFight listener result=%d",
        (int) result
    );

    return result == GUM_ATTACH_OK;
}

/* ========================================================= */
/* CLEANUP                                                   */
/* ========================================================= */

static void remove_hooks(void)
{
    if (g_interceptor == NULL)
        return;

    if (g_update_hook_installed) {
        gum_interceptor_revert(
            g_interceptor,
            GSIZE_TO_POINTER(ADDR_CARMYITEM_UPDATE)
        );

        g_update_hook_installed = FALSE;
    }

    if (g_onaccept_hook_installed) {
        gum_interceptor_revert(
            g_interceptor,
            GSIZE_TO_POINTER(ADDR_OFFER_DIALOG_ONACCEPT)
        );

        g_onaccept_hook_installed = FALSE;
    }

    if (g_ctor_listener != NULL) {
        gum_interceptor_detach(
            g_interceptor,
            g_ctor_listener
        );

        g_ctor_listener = NULL;
    }

    if (g_click_listener != NULL) {
        gum_interceptor_detach(
            g_interceptor,
            g_click_listener
        );

        g_click_listener = NULL;
    }

    if (g_mayfight_listener != NULL) {
        gum_interceptor_detach(
            g_interceptor,
            g_mayfight_listener
        );

        g_mayfight_listener = NULL;
    }

    if (g_branch_patch_installed) {
        gum_memory_patch_code(
            GSIZE_TO_POINTER(ADDR_BUILDUNITLIST_ISARMY_JE),
            2,
            restore_branch_bytes,
            NULL
        );

        g_branch_patch_installed = FALSE;
    }
}

/* ========================================================= */
/* INIT / FINI                                               */
/* ========================================================= */

__attribute__((constructor)) static void nav_condottieri_init(void)
{
    navlog_clear();

    navlog("========================================");
    navlog("loading integrated naval condottieri");

    gum_init_embedded();

    g_interceptor = gum_interceptor_obtain();

    if (g_interceptor == NULL) {
        navlog("ERROR gum_interceptor_obtain");
        return;
    }

    gboolean build_ok =
        install_buildunitlist_patch();

    gboolean update_ok =
        install_armyitem_update_hook();

    gboolean ctor_ok =
        install_ctor_listener();

    gboolean click_ok =
        install_click_listener();

    gboolean accept_ok =
        install_onaccept_hook();

    gboolean mayfight_ok =
        install_mayfight_hook();

    navlog(
        "status BuildList=%s Update=%s Ctor=%s Click=%s Accept=%s MayFight=%s",
        build_ok ? "OK" : "FAIL",
        update_ok ? "OK" : "FAIL",
        ctor_ok ? "OK" : "FAIL",
        click_ok ? "OK" : "FAIL",
        accept_ok ? "OK" : "FAIL",
        mayfight_ok ? "OK" : "FAIL"
    );

    navlog("ready");
}

__attribute__((destructor))
static void
nav_condottieri_fini(void)
{
    remove_hooks();

    gum_deinit_embedded();

    if (g_log_file != NULL) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
}
