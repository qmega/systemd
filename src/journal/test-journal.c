/* SPDX-License-Identifier: LGPL-2.1+ */
/***
  This file is part of systemd.

  Copyright 2011 Lennart Poettering

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include <fcntl.h>
#include <unistd.h>

#include "journal-authenticate.h"
#include "journal-file.h"
#include "journal-vacuum.h"
#include "log.h"
#include "rm-rf.h"

static bool arg_keep = false;

static void test_non_empty(void) {
        dual_timestamp ts;
        JournalFile *f;
        struct iovec iovec;
        static const char test[] = "TEST1=1", test2[] = "TEST2=2";
        Object *o;
        uint64_t p;
        char t[] = "/tmp/journal-XXXXXX";

        log_set_max_level(LOG_DEBUG);

        assert_se(mkdtemp(t));
        assert_se(chdir(t) >= 0);

        assert_se(journal_file_open(-1, "test.journal", O_RDWR|O_CREAT, 0666, true, (uint64_t) -1, true, NULL, NULL, NULL, NULL, &f) == 0);

        dual_timestamp_get(&ts);

        iovec.iov_base = (void*) test;
        iovec.iov_len = strlen(test);
        assert_se(journal_file_append_entry(f, &ts, &iovec, 1, NULL, NULL, NULL) == 0);

        iovec.iov_base = (void*) test2;
        iovec.iov_len = strlen(test2);
        assert_se(journal_file_append_entry(f, &ts, &iovec, 1, NULL, NULL, NULL) == 0);

        iovec.iov_base = (void*) test;
        iovec.iov_len = strlen(test);
        assert_se(journal_file_append_entry(f, &ts, &iovec, 1, NULL, NULL, NULL) == 0);

#if HAVE_GCRYPT
        journal_file_append_tag(f);
#endif
        journal_file_dump(f);

        assert_se(journal_file_next_entry(f, 0, DIRECTION_DOWN, &o, &p) == 1);
        assert_se(le64toh(o->entry.seqnum) == 1);

        assert_se(journal_file_next_entry(f, p, DIRECTION_DOWN, &o, &p) == 1);
        assert_se(le64toh(o->entry.seqnum) == 2);

        assert_se(journal_file_next_entry(f, p, DIRECTION_DOWN, &o, &p) == 1);
        assert_se(le64toh(o->entry.seqnum) == 3);

        assert_se(journal_file_next_entry(f, p, DIRECTION_DOWN, &o, &p) == 0);

        assert_se(journal_file_next_entry(f, 0, DIRECTION_DOWN, &o, &p) == 1);
        assert_se(le64toh(o->entry.seqnum) == 1);

        assert_se(journal_file_find_data_object(f, test, strlen(test), NULL, &p) == 1);
        assert_se(journal_file_next_entry_for_data(f, NULL, 0, p, DIRECTION_DOWN, &o, NULL) == 1);
        assert_se(le64toh(o->entry.seqnum) == 1);

        assert_se(journal_file_next_entry_for_data(f, NULL, 0, p, DIRECTION_UP, &o, NULL) == 1);
        assert_se(le64toh(o->entry.seqnum) == 3);

        assert_se(journal_file_find_data_object(f, test2, strlen(test2), NULL, &p) == 1);
        assert_se(journal_file_next_entry_for_data(f, NULL, 0, p, DIRECTION_UP, &o, NULL) == 1);
        assert_se(le64toh(o->entry.seqnum) == 2);

        assert_se(journal_file_next_entry_for_data(f, NULL, 0, p, DIRECTION_DOWN, &o, NULL) == 1);
        assert_se(le64toh(o->entry.seqnum) == 2);

        assert_se(journal_file_find_data_object(f, "quux", 4, NULL, &p) == 0);

        assert_se(journal_file_move_to_entry_by_seqnum(f, 1, DIRECTION_DOWN, &o, NULL) == 1);
        assert_se(le64toh(o->entry.seqnum) == 1);

        assert_se(journal_file_move_to_entry_by_seqnum(f, 3, DIRECTION_DOWN, &o, NULL) == 1);
        assert_se(le64toh(o->entry.seqnum) == 3);

        assert_se(journal_file_move_to_entry_by_seqnum(f, 2, DIRECTION_DOWN, &o, NULL) == 1);
        assert_se(le64toh(o->entry.seqnum) == 2);

        assert_se(journal_file_move_to_entry_by_seqnum(f, 10, DIRECTION_DOWN, &o, NULL) == 0);

        journal_file_rotate(&f, true, (uint64_t) -1, true, NULL);
        journal_file_rotate(&f, true, (uint64_t) -1, true, NULL);

        (void) journal_file_close(f);

        log_info("Done...");

        if (arg_keep)
                log_info("Not removing %s", t);
        else {
                journal_directory_vacuum(".", 3000000, 0, 0, NULL, true);

                assert_se(rm_rf(t, REMOVE_ROOT|REMOVE_PHYSICAL) >= 0);
        }

        puts("------------------------------------------------------------");
}

static void test_empty(void) {
        JournalFile *f1, *f2, *f3, *f4;
        char t[] = "/tmp/journal-XXXXXX";

        log_set_max_level(LOG_DEBUG);

        assert_se(mkdtemp(t));
        assert_se(chdir(t) >= 0);

        assert_se(journal_file_open(-1, "test.journal", O_RDWR|O_CREAT, 0666, false, (uint64_t) -1, false, NULL, NULL, NULL, NULL, &f1) == 0);

        assert_se(journal_file_open(-1, "test-compress.journal", O_RDWR|O_CREAT, 0666, true, (uint64_t) -1, false, NULL, NULL, NULL, NULL, &f2) == 0);

        assert_se(journal_file_open(-1, "test-seal.journal", O_RDWR|O_CREAT, 0666, false, (uint64_t) -1, true, NULL, NULL, NULL, NULL, &f3) == 0);

        assert_se(journal_file_open(-1, "test-seal-compress.journal", O_RDWR|O_CREAT, 0666, true, (uint64_t) -1, true, NULL, NULL, NULL, NULL, &f4) == 0);

        journal_file_print_header(f1);
        puts("");
        journal_file_print_header(f2);
        puts("");
        journal_file_print_header(f3);
        puts("");
        journal_file_print_header(f4);
        puts("");

        log_info("Done...");

        if (arg_keep)
                log_info("Not removing %s", t);
        else {
                journal_directory_vacuum(".", 3000000, 0, 0, NULL, true);

                assert_se(rm_rf(t, REMOVE_ROOT|REMOVE_PHYSICAL) >= 0);
        }

        (void) journal_file_close(f1);
        (void) journal_file_close(f2);
        (void) journal_file_close(f3);
        (void) journal_file_close(f4);
}

#if HAVE_XZ || HAVE_LZ4
static bool check_compressed(uint64_t compress_threshold, uint64_t data_size) {
        dual_timestamp ts;
        JournalFile *f;
        struct iovec iovec;
        Object *o;
        uint64_t p;
        char t[] = "/tmp/journal-XXXXXX";
        char data[2048] = {0};
        bool is_compressed;
        int r;

        assert_se(data_size <= sizeof(data));

        log_set_max_level(LOG_DEBUG);

        assert_se(mkdtemp(t));
        assert_se(chdir(t) >= 0);

        assert_se(journal_file_open(-1, "test.journal", O_RDWR|O_CREAT, 0666, true, compress_threshold, true, NULL, NULL, NULL, NULL, &f) == 0);

        dual_timestamp_get(&ts);

        iovec.iov_base = (void*) data;
        iovec.iov_len = data_size;
        assert_se(journal_file_append_entry(f, &ts, &iovec, 1, NULL, NULL, NULL) == 0);

#if HAVE_GCRYPT
        journal_file_append_tag(f);
#endif
        journal_file_dump(f);

        /* We have to partially reimplement some of the dump logic, because the normal next_entry does the
         * decompression for us. */
        p = le64toh(f->header->header_size);
        for (;;) {
                r = journal_file_move_to_object(f, OBJECT_UNUSED, p, &o);
                assert_se(r == 0);
                if (o->object.type == OBJECT_DATA)
                        break;

                assert_se(p < le64toh(f->header->tail_object_offset));
                p = p + ALIGN64(le64toh(o->object.size));
        }

        is_compressed = (o->object.flags & OBJECT_COMPRESSION_MASK) != 0;

        (void) journal_file_close(f);

        log_info("Done...");

        if (arg_keep)
                log_info("Not removing %s", t);
        else {
                journal_directory_vacuum(".", 3000000, 0, 0, NULL, true);

                assert_se(rm_rf(t, REMOVE_ROOT|REMOVE_PHYSICAL) >= 0);
        }

        puts("------------------------------------------------------------");

        return is_compressed;
}

static void test_min_compress_size(void) {
        /* Note that XZ will actually fail to compress anything under 80 bytes, so you have to choose the limits
         * carefully */

        /* DEFAULT_MIN_COMPRESS_SIZE is 512 */
        assert_se(!check_compressed((uint64_t) -1, 255));
        assert_se(check_compressed((uint64_t) -1, 513));

        /* compress everything */
        assert_se(check_compressed(0, 96));
        assert_se(check_compressed(8, 96));

        /* Ensure we don't try to compress less than 8 bytes */
        assert_se(!check_compressed(0, 7));

        /* check boundary conditions */
        assert_se(check_compressed(256, 256));
        assert_se(!check_compressed(256, 255));
}
#endif

int main(int argc, char *argv[]) {
        arg_keep = argc > 1;

        /* journal_file_open requires a valid machine id */
        if (access("/etc/machine-id", F_OK) != 0)
                return EXIT_TEST_SKIP;

        test_non_empty();
        test_empty();
#if HAVE_XZ || HAVE_LZ4
        test_min_compress_size();
#endif

        return 0;
}
