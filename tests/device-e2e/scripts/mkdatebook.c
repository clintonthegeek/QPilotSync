/* Creates an empty Palm DatebookDB .pdb (type=DATA, creator=date) using
 * pilot-link. Used to bake a HotSync-ready baseline session that already has
 * a DatebookDB, since a fresh m515 image only creates the DB when the Date
 * Book app is actually used. Build: see make-baseline.sh. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pi-file.h>
#include <pi-dlp.h>

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s out.pdb\n", argv[0]); return 2; }
    struct DBInfo info;
    memset(&info, 0, sizeof(info));
    strncpy(info.name, "DatebookDB", sizeof(info.name) - 1);
    info.flags = dlpDBFlagBackup;
    info.type = 0x44415441UL;    /* 'DATA' */
    info.creator = 0x64617465UL; /* 'date' */
    info.version = 0;
    info.createDate = time(NULL);
    info.modifyDate = time(NULL);
    info.backupDate = 0;
    pi_file_t *pf = pi_file_create(argv[1], &info);
    if (!pf) { fprintf(stderr, "pi_file_create failed\n"); return 1; }
    if (pi_file_close(pf) < 0) { fprintf(stderr, "pi_file_close failed\n"); return 1; }
    printf("wrote %s (DatebookDB, type=DATA creator=date, empty)\n", argv[1]);
    return 0;
}
