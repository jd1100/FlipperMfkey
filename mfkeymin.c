#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LF_POLY_ODD (0x29CE5C)
#define LF_POLY_EVEN (0x870804)
#define BIT(x, n) ((x) >> (n) & 1)
#define BEBIT(x, n) BIT(x, (n) ^ 24)
#define SWAPENDIAN(x) (x = (x >> 8 & 0xff00ff) | (x & 0xff00ff) << 8, x = x >> 16 | x << 16)
typedef struct bucket { uint32_t *head; uint32_t *bp; } bucket_t;

// array of with 2 rows and 256 columns so essentially theres 2 elements each with a buffer of 256 elements each
// so in order to loop through the element you need a nested for loop.
// what is the input we need to sort?

// so the new typedef called "bucket_array_t" is an array of size [2][256] so there are 2 elements with 256 values in each element
// total of 2x256 elements with each element being a bucket_t struct thatd make each element 8 bytes
// so this structure takes up about 4KB 
typedef bucket_t bucket_array_t[2][0x100];

typedef struct bucket_info { struct { uint32_t *head, *tail; } bucket_info[2][0x100]; uint32_t numbuckets; } bucket_info_t;

// struct "Crypto1State" is a 8 byte struct
struct Crypto1State {uint32_t odd, even;};


uint32_t prng_successor(uint32_t x, uint32_t n) {
    SWAPENDIAN(x);
    while (n--)
        x = x >> 1 | (x >> 16 ^ x >> 18 ^ x >> 19 ^ x >> 21) << 31;

    return SWAPENDIAN(x);
}
static inline int filter(uint32_t const x) {
    uint32_t f;
    f  = 0xf22c0 >> (x       & 0xf) & 16;
    f |= 0x6c9c0 >> (x >>  4 & 0xf) &  8;
    f |= 0x3c8b0 >> (x >>  8 & 0xf) &  4;
    f |= 0x1e458 >> (x >> 12 & 0xf) &  2;
    f |= 0x0d938 >> (x >> 16 & 0xf) &  1;
    return BIT(0xEC57E80A, f);
}
static inline uint8_t evenparity32(uint32_t x) {
    return (__builtin_parity(x) & 0xFF);
}
void bucket_sort_intersect(uint32_t *const estart, uint32_t *const estop, uint32_t *const ostart, uint32_t *const ostop, bucket_info_t *bucket_info, bucket_array_t bucket) {
    uint32_t *p1, *p2;
    uint32_t *start[2];
    uint32_t *stop[2];
    start[0] = estart;
    stop[0] = estop;
    start[1] = ostart;
    stop[1] = ostop;
    for (uint32_t i = 0; i < 2; i++) {
        for (uint32_t j = 0x00; j <= 0xff; j++) {
            bucket[i][j].bp = bucket[i][j].head;
        }
    }
    for (uint32_t i = 0; i < 2; i++) {
        for (p1 = start[i]; p1 <= stop[i]; p1++) {
            uint32_t bucket_index = (*p1 & 0xff000000) >> 24;
            *(bucket[i][bucket_index].bp++) = *p1;
        }
    }
    for (uint32_t i = 0; i < 2; i++) {
        p1 = start[i];
        uint32_t nonempty_bucket = 0;
        for (uint32_t j = 0x00; j <= 0xff; j++) {
            if (bucket[0][j].bp != bucket[0][j].head && bucket[1][j].bp != bucket[1][j].head) { // non-empty intersecting buckets only
                bucket_info->bucket_info[i][nonempty_bucket].head = p1;
                for (p2 = bucket[i][j].head; p2 < bucket[i][j].bp; *p1++ = *p2++);
                bucket_info->bucket_info[i][nonempty_bucket].tail = p1 - 1;
                nonempty_bucket++;
            }
        }
        bucket_info->numbuckets = nonempty_bucket;
    }
}
static inline void update_contribution(uint32_t *item, const uint32_t mask1, const uint32_t mask2) {
    uint32_t p = *item >> 25;
    p = p << 1 | (evenparity32(*item & mask1));
    p = p << 1 | (evenparity32(*item & mask2));
    *item = p << 24 | (*item & 0xffffff);
}
static inline void extend_table(uint32_t *tbl, uint32_t **end, int bit, int m1, int m2, uint32_t in) {
    in <<= 24;
    for (*tbl <<= 1; tbl <= *end; *++tbl <<= 1)
        if (filter(*tbl) ^ filter(*tbl | 1)) {
            *tbl |= filter(*tbl) ^ bit;
            update_contribution(tbl, m1, m2);
            *tbl ^= in;
        } else if (filter(*tbl) == bit) {
            *++*end = tbl[1];
            tbl[1] = tbl[0] | 1;
            update_contribution(tbl, m1, m2);
            *tbl++ ^= in;
            update_contribution(tbl, m1, m2);
            *tbl ^= in;
        } else
            *tbl-- = *(*end)--;
}
static inline void extend_table_simple(uint32_t *tbl, uint32_t **end, int bit) {
    for (*tbl <<= 1; tbl <= *end; *++tbl <<= 1) {
        if (filter(*tbl) ^ filter(*tbl | 1)) {
            *tbl |= filter(*tbl) ^ bit;
        } else if (filter(*tbl) == bit) {
            *++*end = *++tbl;
            *tbl = tbl[-1] | 1;
        } else {
            *tbl-- = *(*end)--;
        }
    }
}
static struct Crypto1State *recover(uint32_t *o_head, uint32_t *o_tail, uint32_t oks, uint32_t *e_head, uint32_t *e_tail, uint32_t eks, int rem, struct Crypto1State *sl, uint32_t in, bucket_array_t bucket) {
    bucket_info_t bucket_info;
    if (rem == -1) {
        for (uint32_t *e = e_head; e <= e_tail; ++e) {
            *e = *e << 1 ^ (evenparity32(*e & LF_POLY_EVEN)) ^ (!!(in & 4));
            for (uint32_t *o = o_head; o <= o_tail; ++o, ++sl) {
                sl->even = *o;
                sl->odd = *e ^ (evenparity32(*o & LF_POLY_ODD));
                sl[1].odd = sl[1].even = 0;
            }
        }
        return sl;
    }
    for (uint32_t i = 0; i < 4 && rem--; i++) {
        oks >>= 1;
        eks >>= 1;
        in >>= 2;
        extend_table(o_head, &o_tail, oks & 1, LF_POLY_EVEN << 1 | 1, LF_POLY_ODD << 1, 0);
        if (o_head > o_tail)
            return sl;
        extend_table(e_head, &e_tail, eks & 1, LF_POLY_ODD, LF_POLY_EVEN << 1 | 1, in & 3);
        if (e_head > e_tail)
            return sl;
    }
    bucket_sort_intersect(e_head, e_tail, o_head, o_tail, &bucket_info, bucket);
    for (int i = bucket_info.numbuckets - 1; i >= 0; i--) {
        sl = recover(bucket_info.bucket_info[1][i].head, bucket_info.bucket_info[1][i].tail, oks,
                     bucket_info.bucket_info[0][i].head, bucket_info.bucket_info[0][i].tail, eks,
                     rem, sl, in, bucket);
    }
    return sl;
}
struct Crypto1State *lfsr_recovery32(uint32_t ks2, uint32_t in) {
    
    // 8 bytes
    struct Crypto1State *statelist;
    
    // 4 bytes + 4 bytes + ?
    uint32_t *odd_head = 0, *odd_tail = 0, oks = 0;
    // 4 bytes + 4 bytes + ?
    uint32_t *even_head = 0, *even_tail = 0, eks = 0;
    int i;
    for (i = 31; i >= 0; i -= 2)
        oks = oks << 1 | BEBIT(ks2, i);
    for (i = 30; i >= 0; i -= 2)
        eks = eks << 1 | BEBIT(ks2, i);

    /* not sure what the "odd" is referring to but this assigns the pointer variables to a piece of memory thats 4194304 bytes big
    or about 4MB
    so we have
    - odd_head 4MB
    - odd_tail 4MB
    - even_head 4MB
    - even_tail 4MB
    */
    // allocate 4 * 2^20 bytes
    odd_head = odd_tail = calloc(1, sizeof(uint32_t) << 20);

    // allocate 4 * 2^20 bytes
    even_head = even_tail = calloc(1, sizeof(uint32_t) << 20);
    
    // allocate 8 * 2^18 bytes
    statelist =  calloc(1, sizeof(struct Crypto1State) << 18);

    // initialize the structure properties to 0
    statelist->odd = statelist->even = 0;
    
    // creates new bucket which is defined as a 2x256 array with each element 8 bytes (head and bp properties are uint32_t pointers)
    bucket_array_t bucket;
    for (i = 0; i < 2; i++) {
        // for each column in the row, assign the elemtent to a piece of memory 4 * 2^12 bytes
        for (uint32_t j = 0; j <= 0xff; j++) {
            bucket[i][j].head = calloc(1, sizeof(uint32_t) << 12);
        }
    }
    // set i = to 1 * 2^20 and decrement by 1
    for (i = 1 << 20; i >= 0; --i) {
        if (filter(i) == (oks & 1))
            *++odd_tail = i;
        if (filter(i) == (eks & 1))
            *++even_tail = i;
    }
    for (i = 0; i < 4; i++) {
        extend_table_simple(odd_head,  &odd_tail, (oks >>= 1) & 1);
        extend_table_simple(even_head, &even_tail, (eks >>= 1) & 1);
    }
    in = (in >> 16 & 0xff) | (in << 16) | (in & 0xff00); // Byte swapping
    recover(odd_head, odd_tail, oks, even_head, even_tail, eks, 11, statelist, in << 1, bucket);
    for (i = 0; i < 2; i++)
        for (uint32_t j = 0; j <= 0xff; j++)
            free(bucket[i][j].head);
    free(odd_head);
    free(even_head);
    return statelist;
}
void crypto1_get_lfsr(struct Crypto1State *state, uint64_t *lfsr) {
    int i;
    for (*lfsr = 0, i = 23; i >= 0; --i) {
        *lfsr = *lfsr << 1 | BIT(state->odd, i ^ 3);
        *lfsr = *lfsr << 1 | BIT(state->even, i ^ 3);
    }
}
uint8_t crypt_or_rollback_bit(struct Crypto1State *s, uint32_t in, int x, int is_crypt) {
    uint8_t ret;
    uint32_t feedin, t;
    if (is_crypt == 0) {
        s->odd &= 0xffffff;
        t = s->odd, s->odd = s->even, s->even = t;
    }
    ret = filter(s->odd);
    feedin = ret & (!!x);
    if (is_crypt == 0) {
        feedin ^= s->even & 1;
        feedin ^= LF_POLY_EVEN & (s->even >>= 1);
    } else {
        feedin ^= LF_POLY_EVEN & s->even;
    }
    feedin ^= LF_POLY_ODD & s->odd;
    feedin ^= !!in;
    if (is_crypt == 0) {
        s->even |= (evenparity32(feedin)) << 23;
    } else {
        s->even = s->even << 1 | (evenparity32(feedin));
        t = s->odd, s->odd = s->even, s->even = t;
    }
    return ret;
}
uint32_t crypt_or_rollback_word(struct Crypto1State *s, uint32_t in, int x, int is_crypt) {
    uint32_t ret = 0;
    int i;
    if (is_crypt == 0) {
        for (i = 31; i >= 0; i--) {
            ret |= crypt_or_rollback_bit(s, BEBIT(in, i), x, 0) << (24 ^ i);
        }
    } else {
        for (i = 0; i <= 31; i++) {
            ret |= crypt_or_rollback_bit(s, BEBIT(in, i), x, 1) << (24 ^ i);
        }
    }
    return ret;
}

int main(int argc, char *argv[]) {
    struct Crypto1State *s, *t;
    uint64_t key;     // recovered key
    uint32_t uid;     // serial number
    uint32_t nt0;      // tag challenge first
    uint32_t nt1;      // tag challenge second
    uint32_t nr0_enc; // first encrypted reader challenge
    uint32_t ar0_enc; // first encrypted reader response
    uint32_t nr1_enc; // second encrypted reader challenge
    uint32_t ar1_enc; // second encrypted reader response
    uint32_t ks2;     // keystream used to encrypt reader response
    int i;
    int found;
    char *rest;
    char *token;
    size_t keyarray_size;
    uint64_t *keyarray = malloc(sizeof(uint64_t)*1);

    keyarray_size = 0;
    FILE* filePointer;
    int bufferLength = 255;
    char buffer[bufferLength];
    filePointer = fopen(".mfkey32.log", "r"); // TODO: In FAP, use full path

    while(fgets(buffer, bufferLength, filePointer)) {
        rest = buffer;
        for (i = 0; i <= 17; i++) {
            token = strtok_r(rest, " ", &rest);
            switch(i){
                case 5: sscanf(token, "%x", &uid); break;
                case 7: sscanf(token, "%x", &nt0); break;
                case 9: sscanf(token, "%x", &nr0_enc); break;
                case 11: sscanf(token, "%x", &ar0_enc); break;
                case 13: sscanf(token, "%x", &nt1); break;
                case 15: sscanf(token, "%x", &nr1_enc); break;
                case 17: sscanf(token, "%x", &ar1_enc); break;
                default: break; // Do nothing
            }
        }
        uint32_t p64 = prng_successor(nt0, 64);
        uint32_t p64b = prng_successor(nt1, 64);
        ks2 = ar0_enc ^ p64;
        s = lfsr_recovery32(ar0_enc ^ p64, 0);
        for (t = s; t->odd | t->even; ++t) {
            crypt_or_rollback_word(t, 0, 0, 0);
            crypt_or_rollback_word(t, nr0_enc, 1, 0);
            crypt_or_rollback_word(t, uid ^ nt0, 0, 0);
            crypto1_get_lfsr(t, &key);
            crypt_or_rollback_word(t, uid ^ nt1, 0, 1);
            crypt_or_rollback_word(t, nr1_enc, 1, 1);
            if (ar1_enc == (crypt_or_rollback_word(t, 0, 0, 1) ^ p64b)) {
                int found = 0;
                for(i = 0; i < keyarray_size; i++) {
                    if (keyarray[i] == key) {
                        found = 1;
                        break;
                    }
                }
                if (found == 0) {
                    keyarray = realloc(keyarray, sizeof(uint64_t)*(keyarray_size+1));
                    keyarray_size += 1;
                    keyarray[keyarray_size-1] = key;
                }
                break;
            }
        }
        free(s);
    }

    fclose(filePointer);
    printf("Unique keys found:\n");
    for(i = 0; i < keyarray_size; i++) {
        printf("%012" PRIx64 , keyarray[i]);
        printf("\n");
    }
    // TODO: Append key to user dictionary file if missing in file
    return 0;
}
