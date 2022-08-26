/* MemTest86+ V5 Specific code (GPL V2.0)
 * By Samuel DEMEULEMEESTER, memtest@memtest.org
 * https://x86.fr - https://www.memtest.org
 * ------------------------------------------------
 * Based on JEDEC JEP106-BA - January 2022
 */

#define JEDEC_CONT_CODE_MAX 14

struct spd_jedec_manufacturer {
    uint16_t jedec_code;
    uint16_t offset;
};

extern const char jep106_str_start;
extern uint16_t jep106_cnt;
extern const struct spd_jedec_manufacturer jep106[];

#define JEP106_CNT jep106_cnt

#define JEP106_NAME(i) \
    (&jep106_str_start + jep106[i].offset)
