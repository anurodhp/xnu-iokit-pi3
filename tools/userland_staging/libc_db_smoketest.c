/* Real smoketest for libc_build/libc_db.a (build_libc_db.sh) -- exercises
 * real dbopen(DB_HASH)/put/get/close (not fabricated: this links directly
 * against real Apple db.c and the real hash backend). Same
 * int main(void) + printf() pass/fail marker convention as
 * libc_regex_smoketest.c -- read that file's own header before changing
 * this one.
 */
#include <db.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    DB *db;
    DBT key, data;
    char keybuf[] = "answer";
    char valbuf[] = "42";
    int ok = 1;

    db = dbopen(NULL, O_CREAT | O_RDWR, 0600, DB_HASH, NULL);
    if (db == NULL) {
        printf("dbopen() FAILED\n");
        printf("LIBC_DB_SMOKETEST: FAIL\n");
        return 1;
    }

    key.data = keybuf;
    key.size = sizeof(keybuf);
    data.data = valbuf;
    data.size = sizeof(valbuf);
    if (db->put(db, &key, &data, 0) != 0) {
        printf("put() FAILED\n");
        ok = 0;
    }

    if (ok) {
        memset(&data, 0, sizeof(data));
        if (db->get(db, &key, &data, 0) != 0) {
            printf("get() FAILED\n");
            ok = 0;
        } else if (data.size != sizeof(valbuf) ||
                   memcmp(data.data, valbuf, sizeof(valbuf)) != 0) {
            printf("get() returned wrong value\n");
            ok = 0;
        }
    }

    db->close(db);
    printf(ok ? "LIBC_DB_SMOKETEST: ALL PASS\n" : "LIBC_DB_SMOKETEST: FAIL\n");
    return ok ? 0 : 1;
}
