#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <frida-gum.h>


/* =========================================================
 * OPAQUE EU4 TYPES
 * =========================================================
 *
 * Nie próbujemy odwzorowywać całych klas EU4.
 * Trzymamy tylko wskaźniki do obiektów należących do gry.
 */

typedef void CUnit;
typedef void CNavy;
typedef void CCountry;


/*
 * Na podstawie reverse engineeringu:
 *
 * CCountryTag jest przenoszony przez ABI jako wartość 8-bajtowa.
 *
 * Zaobserwowany layout:
 *
 * +0x00  char tag[4]   np. "TEU\0"
 * +0x04  int16 index
 * +0x06  ???
 *
 * Dla wywołania HasEntry() wygodnie traktujemy całość
 * jako uint64_t.
 */
typedef uint64_t CCountryTag;


/* =========================================================
 * BUILD-SPECIFIC EU4 ADDRESSES / OFFSETS
 * =========================================================
 *
 * To są wartości znalezione dla naszej obecnej wersji EU4.
 *
 * Jeżeli executable zmieni się po aktualizacji,
 * trzeba je ponownie zweryfikować.
 */

/*
 * bool CUnit::MayFight(CUnit const *) const
 */
#define ADDR_MAY_FIGHT \
    0x00000000018fee80ULL


/*
 * NDiplomacy::CRelationsContainerArray<CWar>::HasEntry(
 *     CCountryTag
 * ) const
 */
#define ADDR_WAR_HAS_ENTRY \
    0x0000000000db8724ULL


/*
 * CUnit:
 *
 * +0xc8 -> 8-bajtowy CCountryTag właściciela jednostki
 */
#define OFFSET_UNIT_COUNTRY_TAG \
    0x0c8


/*
 * CCountry:
 *
 * +0x1530 -> embedded war relations container,
 *            który jest "this" dla HasEntry<CWar>().
 */
#define OFFSET_COUNTRY_WARS \
    0x1530


/* =========================================================
 * CONTRACT
 * =========================================================
 */

typedef struct {
    CNavy *navy;

    /*
     * Kraj będący właścicielem floty.
     *
     * Na razie nie jest potrzebny do combat flow,
     * ale zostawiamy pole pod pełną mechanikę.
     */
    CCountry *contractor;

    /*
     * Kraj, który wynajął flotę.
     *
     * To JEGO wojny określają z kim flota może walczyć.
     */
    CCountry *contracter;

    int cost;
} NavalCondottieriContract;


/* =========================================================
 * CONTRACT REGISTRY
 * =========================================================
 */

#define MAX_CONTRACTS 128

static NavalCondottieriContract g_contracts[MAX_CONTRACTS];

static int g_contract_count = 0;


/* =========================================================
 * FIND CONTRACT
 * =========================================================
 */

static NavalCondottieriContract *
find_contract(void *navy)
{
    if (navy == NULL)
        return NULL;


    for (int i = 0; i < g_contract_count; i++) {

        if (g_contracts[i].navy == navy)
            return &g_contracts[i];
    }


    return NULL;
}


/* =========================================================
 * ADD CONTRACT
 * =========================================================
 */

static bool
add_contract(
    CNavy *navy,
    CCountry *contractor,
    CCountry *contracter,
    int cost
)
{
    if (navy == NULL) {

        printf(
            "[nav-condottieri] add_contract: navy == NULL\n"
        );

        return false;
    }


    if (contracter == NULL) {

        printf(
            "[nav-condottieri] add_contract: renter == NULL\n"
        );

        return false;
    }


    if (g_contract_count >= MAX_CONTRACTS) {

        printf(
            "[nav-condottieri] add_contract: registry full\n"
        );

        return false;
    }


    if (find_contract(navy) != NULL) {

        printf(
            "[nav-condottieri] add_contract: navy %p already registered\n",
            navy
        );

        return false;
    }


    NavalCondottieriContract *contract =
        &g_contracts[g_contract_count];


    memset(
        contract,
        0,
        sizeof(*contract)
    );


    contract->navy = navy;

    contract->contractor = contractor;

    contract->contracter = contracter;

    contract->cost = cost;


    g_contract_count++;


    printf(
        "[nav-condottieri] contract ADDED\n"
    );

    printf(
        "[nav-condottieri]   navy   = %p\n",
        navy
    );

    printf(
        "[nav-condottieri]   owner  = %p\n",
        contractor
    );

    printf(
        "[nav-condottieri]   renter = %p\n",
        contracter
    );

    printf(
        "[nav-condottieri]   count  = %d\n",
        g_contract_count
    );


    fflush(stdout);


    return true;
}


/* =========================================================
 * REMOVE CONTRACT
 * =========================================================
 */

static bool
remove_contract(CNavy *navy)
{
    for (int i = 0; i < g_contract_count; i++) {

        if (g_contracts[i].navy != navy)
            continue;


        /*
         * Przenosimy ostatni element w miejsce usuwanego.
         *
         * Nie potrzebujemy zachowywać kolejności registry.
         */
        g_contracts[i] =
            g_contracts[g_contract_count - 1];


        memset(
            &g_contracts[g_contract_count - 1],
            0,
            sizeof(g_contracts[0])
        );


        g_contract_count--;


        printf(
            "[nav-condottieri] contract REMOVED navy=%p count=%d\n",
            navy,
            g_contract_count
        );


        fflush(stdout);


        return true;
    }


    return false;
}


/* =========================================================
 * COUNTRY TAG
 * =========================================================
 */

static CCountryTag
get_unit_country_tag(CUnit *unit)
{
    CCountryTag tag = 0;


    if (unit == NULL)
        return 0;


    /*
     * Nie robimy:
     *
     * *(CCountryTag *)(unit + ...)
     *
     * tylko memcpy, żeby uniknąć problemów z aliasingiem /
     * alignmentem.
     */
    memcpy(
        &tag,
        (char *)unit + OFFSET_UNIT_COUNTRY_TAG,
        sizeof(tag)
    );


    return tag;
}


/* =========================================================
 * TAG DEBUGGING
 * =========================================================
 */

static void
tag_to_string(
    CCountryTag tag,
    char out[4]
)
{
    uint8_t bytes[8];


    memcpy(
        bytes,
        &tag,
        sizeof(bytes)
    );


    out[0] = (char)bytes[0];
    out[1] = (char)bytes[1];
    out[2] = (char)bytes[2];
    out[3] = '\0';
}


static uint16_t
tag_get_index(
    CCountryTag tag
)
{
    uint8_t bytes[8];

    uint16_t index = 0;


    memcpy(
        bytes,
        &tag,
        sizeof(bytes)
    );


    memcpy(
        &index,
        bytes + 4,
        sizeof(index)
    );


    return index;
}


/* =========================================================
 * EU4 WAR RELATIONS
 * =========================================================
 */

/*
 * Native signature inferred from disassembly:
 *
 * bool HasEntry(
 *     CRelationsContainerArray<CWar> *this,
 *     CCountryTag target
 * );
 *
 * SysV:
 *
 * RDI = this
 * RSI = CCountryTag
 * AL  = bool return
 */

typedef bool (*HasWarEntryFn)(
    void *self,
    CCountryTag target
);


static HasWarEntryFn g_has_war_entry =
    (HasWarEntryFn)ADDR_WAR_HAS_ENTRY;


/* =========================================================
 * COUNTRY AT WAR WITH
 * =========================================================
 */

static bool
country_at_war_with(
    CCountry *country,
    CCountryTag target
)
{
    if (country == NULL)
        return false;


    /*
     * Embedded relations container.
     *
     * Nie dereferencjonujemy pointera z +0x1530.
     *
     * Sam adres:
     *
     *     country + 0x1530
     *
     * jest "this" dla HasEntry().
     */
    void *wars =
        (void *)(
            (char *)country +
            OFFSET_COUNTRY_WARS
        );


    bool result =
        g_has_war_entry(
            wars,
            target
        );


    return result;
}


/* =========================================================
 * CONTRACT COMBAT LOGIC
 * =========================================================
 */

static bool
contract_may_fight(
    CUnit *unit_a,
    CUnit *unit_b,
    NavalCondottieriContract *contract_a,
    NavalCondottieriContract *contract_b
)
{
    /*
     * Owner tags obu jednostek.
     */
    CCountryTag tag_a =
        get_unit_country_tag(unit_a);

    CCountryTag tag_b =
        get_unit_country_tag(unit_b);


    char tag_a_str[4];
    char tag_b_str[4];


    tag_to_string(
        tag_a,
        tag_a_str
    );


    tag_to_string(
        tag_b,
        tag_b_str
    );


    printf(
        "[nav-condottieri] combat check\n"
    );

    printf(
        "[nav-condottieri]   A=%p tag=%s index=%u contract=%s\n",
        unit_a,
        tag_a_str,
        (unsigned)tag_get_index(tag_a),
        contract_a != NULL ? "YES" : "NO"
    );

    printf(
        "[nav-condottieri]   B=%p tag=%s index=%u contract=%s\n",
        unit_b,
        tag_b_str,
        (unsigned)tag_get_index(tag_b),
        contract_b != NULL ? "YES" : "NO"
    );


    /*
     * =====================================================
     * A jest wynajętą flotą.
     * =====================================================
     *
     * Sprawdzamy:
     *
     * renter(A) at war with owner(B)?
     */

    if (contract_a != NULL) {

        printf(
            "[nav-condottieri]   A renter=%p checking war vs %s\n",
            contract_a->contracter,
            tag_b_str
        );


        bool renter_a_at_war =
            country_at_war_with(
                contract_a->contracter,
                tag_b
            );


        printf(
            "[nav-condottieri]   A renter war result=%d\n",
            renter_a_at_war ? 1 : 0
        );


        if (renter_a_at_war) {

            printf(
                "[nav-condottieri] RESULT: MAY FIGHT\n"
            );

            fflush(stdout);

            return true;
        }
    }


    /*
     * =====================================================
     * B jest wynajętą flotą.
     * =====================================================
     *
     * Musimy sprawdzić również drugą stronę.
     *
     * To jest ważne, bo nie zakładamy, w jakiej kolejności
     * EU4 poda jednostki do MayFight().
     */

    if (contract_b != NULL) {

        printf(
            "[nav-condottieri]   B renter=%p checking war vs %s\n",
            contract_b->contracter,
            tag_a_str
        );


        bool renter_b_at_war =
            country_at_war_with(
                contract_b->contracter,
                tag_a
            );


        printf(
            "[nav-condottieri]   B renter war result=%d\n",
            renter_b_at_war ? 1 : 0
        );


        if (renter_b_at_war) {

            printf(
                "[nav-condottieri] RESULT: MAY FIGHT\n"
            );

            fflush(stdout);

            return true;
        }
    }


    /*
     * Jest kontrakt, ale renter nie jest w wojnie
     * z właścicielem drugiej jednostki.
     *
     * W tym przypadku override = FALSE combat result.
     *
     * Czyli nie pozwalamy vanilla ownerowi floty
     * decydować na podstawie jego własnych wojen.
     */
    printf(
        "[nav-condottieri] RESULT: MAY NOT FIGHT\n"
    );


    fflush(stdout);


    return false;
}


/* =========================================================
 * EXPORTED TEST API
 * =========================================================
 */


/*
 * GDB:
 *
 * navcond_test_contract(navy, renter)
 *
 * contractor zostawiamy NULL.
 */
__attribute__((visibility("default")))
int
navcond_test_contract(
    CNavy *navy,
    CCountry *renter
)
{
    printf(
        "[nav-condottieri] navcond_test_contract\n"
    );

    printf(
        "[nav-condottieri]   navy=%p\n",
        navy
    );

    printf(
        "[nav-condottieri]   renter=%p\n",
        renter
    );


    fflush(stdout);


    return add_contract(
        navy,
        NULL,
        renter,
        0
    ) ? 1 : 0;
}


/*
 * Usunięcie testowego kontraktu.
 */
__attribute__((visibility("default")))
int
navcond_remove_test_contract(
    CNavy *navy
)
{
    return remove_contract(navy) ? 1 : 0;
}


/*
 * Czy podany pointer jest w registry.
 */
__attribute__((visibility("default")))
int
navcond_has_contract(
    CNavy *navy
)
{
    return
        find_contract(navy) != NULL
        ? 1
        : 0;
}


/*
 * Debug helper.
 *
 * Pozwala przetestować:
 *
 * renter + enemy unit -> war relation
 *
 * bez czekania aż odpali MayFight().
 */
__attribute__((visibility("default")))
int
navcond_test_war_against_unit(
    CCountry *renter,
    CUnit *enemy
)
{
    if (renter == NULL || enemy == NULL)
        return -1;


    CCountryTag tag =
        get_unit_country_tag(enemy);


    char tag_str[4];


    tag_to_string(
        tag,
        tag_str
    );


    printf(
        "[nav-condottieri] TEST WAR renter=%p enemy=%p tag=%s index=%u\n",
        renter,
        enemy,
        tag_str,
        (unsigned)tag_get_index(tag)
    );


    bool result =
        country_at_war_with(
            renter,
            tag
        );


    printf(
        "[nav-condottieri] TEST WAR result=%d\n",
        result ? 1 : 0
    );


    fflush(stdout);


    return result ? 1 : 0;
}


/*
 * Debug helper do sprawdzania taga jednostki.
 *
 * Return = country index.
 */
__attribute__((visibility("default")))
int
navcond_dump_unit_tag(
    CUnit *unit
)
{
    if (unit == NULL)
        return -1;


    CCountryTag tag =
        get_unit_country_tag(unit);


    char tag_str[4];


    tag_to_string(
        tag,
        tag_str
    );


    uint16_t index =
        tag_get_index(tag);


    printf(
        "[nav-condottieri] UNIT %p owner tag=%s index=%u raw=0x%016llx\n",
        unit,
        tag_str,
        (unsigned)index,
        (unsigned long long)tag
    );


    fflush(stdout);


    return (int)index;
}


/* =========================================================
 * MAYFIGHT INVOCATION STATE
 * =========================================================
 */

typedef struct {
    bool override_return;
    bool return_value;
} MayFightCallState;


/* =========================================================
 * MAYFIGHT ENTER
 * =========================================================
 */

static void
mayfight_on_enter(
    GumInvocationContext *ic,
    gpointer user_data
)
{
    (void)user_data;


    /*
     * bool CUnit::MayFight(
     *     CUnit const *other
     * ) const
     *
     * x86-64 SysV:
     *
     * argument 0 = this
     * argument 1 = other
     */

    CUnit *unit_a =
        (CUnit *)
        gum_invocation_context_get_nth_argument(
            ic,
            0
        );


    CUnit *unit_b =
        (CUnit *)
        gum_invocation_context_get_nth_argument(
            ic,
            1
        );


    /*
     * Per-call storage.
     *
     * Ten sam blok danych będzie dostępny w on_leave().
     */
    MayFightCallState *state =
        (MayFightCallState *)
        gum_invocation_context_get_listener_invocation_data(
            ic,
            sizeof(MayFightCallState)
        );


    if (state == NULL) {

        printf(
            "[nav-condottieri] ERROR: invocation state == NULL\n"
        );

        fflush(stdout);

        return;
    }


    state->override_return = false;
    state->return_value = false;


    if (unit_a == NULL || unit_b == NULL)
        return;


    NavalCondottieriContract *contract_a =
        find_contract(unit_a);


    NavalCondottieriContract *contract_b =
        find_contract(unit_b);


    /*
     * Żadna jednostka nie jest naval condottieri.
     *
     * Nie dotykamy wyniku.
     *
     * Vanilla MayFight działa dokładnie jak wcześniej.
     */
    if (
        contract_a == NULL &&
        contract_b == NULL
    ) {
        return;
    }


    /*
     * Co najmniej jedna jednostka należy do naszego systemu.
     *
     * Wtedy vanilla MayFight zostanie wykonany,
     * ale jego wynik zastąpimy w on_leave().
     */
    state->return_value =
        contract_may_fight(
            unit_a,
            unit_b,
            contract_a,
            contract_b
        );


    state->override_return = true;
}


/* =========================================================
 * MAYFIGHT LEAVE
 * =========================================================
 */

static void
mayfight_on_leave(
    GumInvocationContext *ic,
    gpointer user_data
)
{
    (void)user_data;


    MayFightCallState *state =
        (MayFightCallState *)
        gum_invocation_context_get_listener_invocation_data(
            ic,
            sizeof(MayFightCallState)
        );


    if (state == NULL) {

        printf(
            "[nav-condottieri] ERROR: leave state == NULL\n"
        );

        fflush(stdout);

        return;
    }


    /*
     * Nie nasza jednostka -> zostaw vanilla return.
     */
    if (!state->override_return)
        return;


    printf(
        "[nav-condottieri] overriding MayFight return -> %d\n",
        state->return_value ? 1 : 0
    );


    fflush(stdout);


    /*
     * bool return znajduje się w RAX/AL.
     *
     * Gum traktuje return jako gpointer-sized value.
     */
    gum_invocation_context_replace_return_value(
        ic,
        GSIZE_TO_POINTER(
            state->return_value
            ? 1
            : 0
        )
    );
}


/* =========================================================
 * HOOK
 * =========================================================
 */

static GumInvocationListener *g_mayfight_listener =
    NULL;


static void
install_mayfight_hook(void)
{
    GumInterceptor *interceptor =
        gum_interceptor_obtain();


    if (interceptor == NULL) {

        printf(
            "[nav-condottieri] ERROR: gum_interceptor_obtain failed\n"
        );

        fflush(stdout);

        return;
    }


    g_mayfight_listener =
        gum_make_call_listener(
            mayfight_on_enter,
            mayfight_on_leave,
            NULL,
            NULL
        );


    if (g_mayfight_listener == NULL) {

        printf(
            "[nav-condottieri] ERROR: gum_make_call_listener failed\n"
        );

        fflush(stdout);

        return;
    }


    gum_interceptor_begin_transaction(
        interceptor
    );


    GumAttachReturn result =
        gum_interceptor_attach(
            interceptor,
            GSIZE_TO_POINTER(
                ADDR_MAY_FIGHT
            ),
            g_mayfight_listener,
            NULL
        );


    gum_interceptor_end_transaction(
        interceptor
    );


    if (result == GUM_ATTACH_OK) {

        printf(
            "[nav-condottieri] MayFight hook installed @ %p\n",
            (void *)ADDR_MAY_FIGHT
        );

    } else {

        printf(
            "[nav-condottieri] ERROR: MayFight hook attach result=%d\n",
            result
        );
    }


    fflush(stdout);
}


/* =========================================================
 * INIT
 * =========================================================
 */

__attribute__((constructor))
static void
nav_condottieri_init(void)
{
    printf(
        "\n"
        "[nav-condottieri] ========================================\n"
    );

    printf(
        "[nav-condottieri] loading\n"
    );


    gum_init_embedded();


    printf(
        "[nav-condottieri] Frida Gum initialized\n"
    );


    printf(
        "[nav-condottieri] MayFight        = %p\n",
        (void *)ADDR_MAY_FIGHT
    );


    printf(
        "[nav-condottieri] War HasEntry    = %p\n",
        (void *)ADDR_WAR_HAS_ENTRY
    );


    printf(
        "[nav-condottieri] unit tag offset = 0x%x\n",
        OFFSET_UNIT_COUNTRY_TAG
    );


    printf(
        "[nav-condottieri] wars offset     = 0x%x\n",
        OFFSET_COUNTRY_WARS
    );


    fflush(stdout);


    install_mayfight_hook();


    printf(
        "[nav-condottieri] ready\n"
    );

    printf(
        "[nav-condottieri] ========================================\n"
        "\n"
    );


    fflush(stdout);
}