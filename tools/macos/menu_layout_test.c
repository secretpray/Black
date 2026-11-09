#undef NDEBUG
#include "../../ports/macos/overrides/menu_pages.h"
#include <assert.h>
#include <stdio.h>

static void expect_row(int page, int index, int row)
{
    assert(black_menu_row(page, index) == row);
    assert(black_menu_index(page, row) == index);
}

int main(void)
{
    /* The Saves page: the profile's slots, the one checkpoint switch, backups. */
    assert(BLACK_PROFILE_SLOTS == 3);
    assert(black_menu_count(PAGE_SAVES) == 5);
    expect_row(PAGE_SAVES, 0, ROW_SAVE_FIRST);
    expect_row(PAGE_SAVES, 1, ROW_SAVE_FIRST + 1);
    expect_row(PAGE_SAVES, 2, ROW_SAVE_FIRST + 2);
    expect_row(PAGE_SAVES, 3, ROW_CHECKPOINTS);
    expect_row(PAGE_SAVES, 4, ROW_NAV_BACKUPS);
    /* The switch is a setting of the Saves section, not a link or a save row. */
    assert(black_menu_save(ROW_CHECKPOINTS));
    assert(!black_menu_link(ROW_CHECKPOINTS));
    assert(black_menu_link(ROW_NAV_SAVES));
    assert(!black_menu_save(ROW_NAV_SAVES));

    /* The slot page lists what port_menu.m found for this slot, 1..6 rows. */
    black_menu_slot_rows[0] = ROW_SLOT_MISSIONS;
    black_menu_slot_rows[1] = ROW_SLOT_CHECKPOINT;
    black_menu_slot_rows[2] = ROW_SLOT_CHECKPOINT_DELETE;
    black_menu_slot_rows[3] = ROW_SLOT_IMPORT;
    black_menu_slot_rows[4] = ROW_SLOT_EXPORT;
    black_menu_slot_rows[5] = ROW_SLOT_DELETE;
    black_menu_slot_count = 6;
    assert(black_menu_count(PAGE_SLOT) == 6);
    expect_row(PAGE_SLOT, 1, ROW_SLOT_CHECKPOINT);
    expect_row(PAGE_SLOT, 2, ROW_SLOT_CHECKPOINT_DELETE);
    expect_row(PAGE_SLOT, 5, ROW_SLOT_DELETE);
    black_menu_slot_count = 3;
    assert(black_menu_count(PAGE_SLOT) == 3 && black_menu_row(PAGE_SLOT, 3) == -1);
    black_menu_slot_count = 0;
    assert(black_menu_count(PAGE_SLOT) == 1 && black_menu_row(PAGE_SLOT, 0) == ROW_SLOT_MISSIONS);
    black_menu_slot_count = 9;
    assert(black_menu_count(PAGE_SLOT) == 6);
    black_menu_slot_count = 1;
    black_menu_slot_rows[0] = ROW_SLOT_MISSIONS;

    /* The top menu and the way back from the Saves pages. */
    assert(black_menu_count(PAGE_ROOT) == 6);
    expect_row(PAGE_ROOT, 3, ROW_NAV_SAVES);
    assert(black_menu_tab(PAGE_ROOT) == -1);
    assert(black_menu_tab(PAGE_SAVES) == 3 && black_menu_tab(PAGE_SLOT) == 3 &&
           black_menu_tab(PAGE_BACKUPS) == 3 && black_menu_tab(PAGE_CONFIRM) == 3);
    assert(black_menu_tab(PAGE_PORT) == 0 && black_menu_tab(PAGE_MODS) == 2 &&
           black_menu_tab(PAGE_CONTROLS) == 1 && black_menu_tab(PAGE_DIAGNOSTICS) == 1);
    assert(black_menu_top(0) == PAGE_PORT && black_menu_top(1) == PAGE_CONTROLS &&
           black_menu_top(2) == PAGE_MODS && black_menu_top(3) == PAGE_SAVES);
    {
        int row = 123;
        assert(black_menu_parent(PAGE_SLOT, &row) == PAGE_SAVES && row == -1);
    }
    {
        int row = 0;
        assert(black_menu_parent(PAGE_IMPORT_PICK, &row) == PAGE_SLOT && row == ROW_SLOT_IMPORT);
    }
    {
        int row = 0;
        assert(black_menu_parent(PAGE_MISSIONS, &row) == PAGE_SLOT && row == ROW_SLOT_MISSIONS);
    }
    black_menu_confirm_page = PAGE_SLOT;
    black_menu_confirm_row = ROW_SLOT_CHECKPOINT_DELETE;
    {
        int row = 0;
        assert(black_menu_parent(PAGE_CONFIRM, &row) == PAGE_SLOT && row == ROW_SLOT_CHECKPOINT_DELETE);
    }

    /* Delete-all is on the backups page only while there are backups. */
    black_menu_backup_rows = 0;
    assert(black_menu_count(PAGE_BACKUPS) == 1 && black_menu_row(PAGE_BACKUPS, 0) == ROW_BACKUP_FIRST);
    black_menu_backup_rows = 3;
    assert(black_menu_count(PAGE_BACKUPS) == 4);
    expect_row(PAGE_BACKUPS, 2, ROW_BACKUP_FIRST + 2);
    expect_row(PAGE_BACKUPS, 3, ROW_BACKUPS_CLEAR);

    /* Every reachable row of every page is distinct and maps back to its index. */
    for (int page = 0; page < MENU_PAGES; ++page)
        for (int i = 0; i < black_menu_count(page); ++i) {
            const int row = black_menu_row(page, i);
            assert(row >= 0 && black_menu_index(page, row) == i);
            for (int j = 0; j < i; ++j) assert(black_menu_row(page, j) != row);
        }
    puts("Menu layout: Saves rows, slot page clamp, parents and round-trip: PASS");
    return 0;
}
