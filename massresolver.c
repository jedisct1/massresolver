#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <ldns/ldns.h>
#include <unbound.h>

#ifndef  MAX_RUNNING
# define MAX_RUNNING 640U
#endif

#if 0
# define USE_LOCALHOST
#endif
#if 0
# define USE_RESOLVCONF
#endif

#if 0
# define USE_TSV
#endif

#ifndef QTYPE
# define QTYPE LDNS_RR_TYPE_A
#endif

static struct ub_ctx         *ctx;
static volatile unsigned int  running = 0U;

static char *
get_one(void)
{
    static char  previous[4096U];
    static char  line[4096U];
    char        *epnt;
    FILE        *fp = stdin;
    char        *pnt;

    while (fgets(line, sizeof line, fp) != NULL) {
#ifdef USE_TSV
        if ((pnt = strrchr(line, ' ')) == NULL || *++pnt == 0) {
            continue;
        }
#else
        if ((pnt = strchr(line, ' ')) != NULL ||
            (pnt = strchr(line, '\t')) != NULL) {
            *pnt = 0;
        }
        pnt = line;
#endif
        if ((epnt = strrchr(pnt, '\n')) != NULL) {
            *epnt = 0;
        }
        if (strcasecmp(pnt, previous) == 0) {
            continue;
        }
        const size_t pnt_len = strlen(pnt) + (size_t) 1U;
        assert(sizeof previous >= pnt_len);
        memcpy(previous, pnt, pnt_len);

        return pnt;
    }
    return NULL;
}

static unsigned int
decrement_running(void)
{
    unsigned int current_running, previous_running, new_running;

    do {
        current_running = running;
    } while (__sync_val_compare_and_swap(&running, current_running,
                                         current_running) != current_running);
    for(;;) {
        if ((new_running = current_running) > 0U) {
            new_running--;
        }
        if ((previous_running =
             __sync_val_compare_and_swap(&running, current_running,
                                         new_running)) == current_running) {
            return new_running;
        }
        current_running = previous_running;
    }
}

static unsigned int
increment_running(void)
{
    return __sync_add_and_fetch(&running, 1U);
}

static void
mycallback(void *mydata, int err, struct ub_result *result)
{
    unsigned int  current_running = decrement_running();

    (void) mydata;
    if (err != 0) {
        fprintf(stderr, "resolve error: %s\n", ub_strerror(err));
        ub_resolve_free(result);
        return;
    }
    if (result->havedata) {
        ldns_buffer  *output;
        ldns_pkt     *packet = NULL;
        ldns_rdf     *rdf;
        ldns_rr      *answer;
        ldns_rr_list *answers = NULL;
        uint8_t      *wire_packet = result->answer_packet;
        size_t        answers_count = 0U;
        size_t        i;
        size_t        wire_packet_len = (size_t) result->answer_len;
        ldns_status   status;

        status = ldns_wire2pkt(&packet, wire_packet, wire_packet_len);
        if (status == LDNS_STATUS_OK) {
            answers = ldns_pkt_answer(packet);
            answers_count = ldns_rr_list_rr_count(answers);
        }
        for (i = (size_t) 0U; i < answers_count; i++) {
            answer = ldns_rr_list_rr(answers, i);
            if (ldns_rr_get_type(answer) != QTYPE) {
                continue;
            }
            output = ldns_buffer_new(wire_packet_len);
            rdf = ldns_rr_rdf(answer, 0);
            if (QTYPE == LDNS_RR_TYPE_A) {
                ldns_rdf2buffer_str_a(output, rdf);
            } else if (QTYPE == LDNS_RR_TYPE_AAAA) {
                ldns_rdf2buffer_str_aaaa(output, rdf);
            } else if (QTYPE == LDNS_RR_TYPE_TXT) {
                ldns_rdf2buffer_str_str(output, rdf);
            } else if (QTYPE == LDNS_RR_TYPE_NS) {
                ldns_rdf2buffer_str_dname(output, rdf);
            } else {
                assert(0);
            }
            printf("%s %s %" PRIu32 "\n", result->qname, ldns_buffer_begin(output),
                   ldns_rr_ttl(answer));
            ldns_buffer_free(output);
        }
        ldns_pkt_free(packet);
        fflush(stdout);
    }
    ub_resolve_free(result);

    char *name;
    while (current_running < MAX_RUNNING && (name = get_one()) != NULL) {
        int retval = ub_resolve_async(ctx, name,
                                      QTYPE, LDNS_RR_CLASS_IN,
                                      NULL, mycallback, NULL);
        if (retval != 0) {
            fprintf(stderr, "resolve error: %s\n", ub_strerror(retval));
            continue;
        }
        current_running = increment_running();
    }
}

int
main(void)
{
    int retval;

    ctx = ub_ctx_create();
    if (!ctx) {
        fprintf(stderr, "error: could not create unbound context\n");
        return 1;
    }
    ub_ctx_async(ctx, 1);
#ifdef USE_RESOLVCONF
    ub_ctx_resolvconf(ctx, NULL);
#elif defined(USE_LOCALHOST)
    ub_ctx_set_fwd(ctx, "127.0.0.1");
#else
    ub_ctx_set_option(ctx, "root-hints:", "named.cache");
#endif
    ub_ctx_set_option(ctx, "num-threads:", "8");
    ub_ctx_set_option(ctx, "outgoing-range:", "8192");
    ub_ctx_set_option(ctx, "outgoing-num-tcp:", "100");
    ub_ctx_set_option(ctx, "outgoing-num-udp:", "100");
    ub_ctx_set_option(ctx, "so-rcvbuf:", "8m");
    ub_ctx_set_option(ctx, "so-sndbuf:", "8m");
    ub_ctx_set_option(ctx, "msg-cache-size:", "500m");
    ub_ctx_set_option(ctx, "msg-cache-slabs:", "8");
    ub_ctx_set_option(ctx, "num-queries-per-thread:", "4096");
    ub_ctx_set_option(ctx, "rrset-cache-size:", "1000m");
    ub_ctx_set_option(ctx, "rrset-cache-slabs:", "8");
    ub_ctx_set_option(ctx, "cache-min-ttl:", "30");
    ub_ctx_set_option(ctx, "infra-cache-slabs:", "8");
    ub_ctx_set_option(ctx, "do-ip6:", "no");
    ub_ctx_set_option(ctx, "minimal-responses:", "yes");
    ub_ctx_set_option(ctx, "key-cache-slabs:", "8");

    retval = ub_resolve_async(ctx, "nonexistent.example.com",
                              QTYPE, LDNS_RR_CLASS_IN,
                              NULL, mycallback, NULL);
    if (retval != 0) {
        fprintf(stderr, "resolve error: %s\n", ub_strerror(retval));
        return 1;
    }
    increment_running();
    ub_wait(ctx);

    return 0;
}
