/*
 * permissive-all: load an Android binary sepolicy, mark every type as
 * permissive, write it back. Compact clone of what sepolicy-inject does.
 *
 * build: gcc -O2 -o permissive-all permissive-all.c -lsepol
 * usage: permissive-all in.sepolicy out.sepolicy
 */

#include <sepol/policydb/policydb.h>
#include <sepol/policydb/expand.h>
#include <sepol/policydb/hashtab.h>
#include <sepol/policydb/ebitmap.h>
#include <sepol/policydb/util.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static policydb_t policy;

static int cb_set_permissive(hashtab_key_t k, hashtab_datum_t d, void *arg) {
    type_datum_t *t = (type_datum_t *)d;
    (void)arg;
    if (t->flavor == TYPE_TYPE) {
        if (ebitmap_set_bit(&policy.permissive_map, t->s.value, 1) < 0) {
            fprintf(stderr, "ebitmap_set_bit failed for %s\n", (char *)k);
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s in.sepolicy out.sepolicy\n", argv[0]);
        return 1;
    }
    const char *in = argv[1], *out = argv[2];
    int fd = open(in, O_RDONLY);
    if (fd < 0) { perror(in); return 1; }
    struct stat st; fstat(fd, &st);
    void *map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) { perror("mmap"); return 1; }
    close(fd);

    struct policy_file pf;
    policy_file_init(&pf);
    pf.type = PF_USE_MEMORY;
    pf.data = map;
    pf.len = st.st_size;
    if (policydb_init(&policy)) { fprintf(stderr, "policydb_init failed\n"); return 1; }
    if (policydb_read(&policy, &pf, 0)) { fprintf(stderr, "policydb_read failed\n"); return 1; }
    fprintf(stderr, "loaded policy: version=%d, %u types\n",
            policy.policyvers, policy.p_types.nprim);

    hashtab_map(policy.p_types.table, cb_set_permissive, NULL);

    /* count set bits for report */
    size_t n = 0;
    for (unsigned v = 1; v <= policy.p_types.nprim; ++v) {
        if (ebitmap_get_bit(&policy.permissive_map, v)) n++;
    }
    fprintf(stderr, "marked %zu types permissive\n", n);

    FILE *fp = fopen(out, "wb");
    if (!fp) { perror(out); return 1; }
    struct policy_file pfo;
    policy_file_init(&pfo);
    pfo.type = PF_USE_STDIO;
    pfo.fp = fp;
    if (policydb_write(&policy, &pfo)) { fprintf(stderr, "policydb_write failed\n"); return 1; }
    fclose(fp);
    fprintf(stderr, "wrote %s\n", out);
    return 0;
}
