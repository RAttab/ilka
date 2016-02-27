/* region_shared.c
   Rémi Attab (remi.attab@gmail.com), 27 Feb 2016
   FreeBSD-style copyright and disclaimer apply
*/


// -----------------------------------------------------------------------------
// region
// -----------------------------------------------------------------------------

static struct ilka_region *
region_shrd_open(const char *file, struct ilka_options *options)
{
    struct ilka_region *r = region_alloc(file, options);
    if (!r) return NULL;

    if ((r->fd = file_open_shm(file, &r->options)) == -1) goto fail_open;
    if ((r->len = file_grow(r->fd, ILKA_PAGE_SIZE)) == -1UL) goto fail_grow;
    if (!mmap_init(&r->mmap, r->fd, r->len, &r->options)) goto fail_mmap;

    const struct meta *meta = meta_init(r);
    if (!meta) goto fail_meta;

    if (!alloc_init(&r->alloc, r, &r->options, meta->alloc)) goto fail_alloc;
    ilka_todo{ if (!epoch_init(&r->epoch, r, &r->options)) goto fail_epoch; }
    if (ILKA_MCHECK) mcheck_init(&r->mcheck);

    r->header_len = alloc_end(&r->alloc);

    return r;

  fail_epoch:
  fail_alloc:
  fail_meta:
    persist_close(&r->persist);

  fail_persist:
    mmap_close(&r->mmap);

  fail_mmap:
  fail_grow:
    file_close(r->fd);

  fail_open:
    free(r);
    return NULL;
}

static bool region_shrd_close(struct ilka_region *r)
{
    ilka_todo{ epoch_close(&r->epoch); }
    persist_close(&r->persist);

    if (!mmap_close(&r->mmap)) return false;
    if (!file_close(r->fd)) return false;
    free(r);

    return true;
}

static bool region_shrd_rm(struct ilka_region *r)
{
    const char *file = r->file;
    if (!ilka_close(r)) return false;
    return file_rm_shm(file);
}


// -----------------------------------------------------------------------------
// persist
// -----------------------------------------------------------------------------

static bool region_shrd_save(struct ilka_region *r)
{
    (void) r;
    ilka_fail("unable to save shrd region");
    return false;
}

static void region_shrd_mark(struct ilka_region *r, ilka_off_t off, size_t len)
{
    (void) r, (void) off, (void) len;
}


// -----------------------------------------------------------------------------
// epoch
// -----------------------------------------------------------------------------

static bool region_shrd_enter(struct ilka_region *r)
{
    return epoch_shrd_enter(&r->epoch);
}

static void region_shrd_exit(struct ilka_region *r)
{
    epoch_shrd_exit(&r->epoch);
}

static bool region_shrd_defer(struct ilka_region *r, void (*fn) (void *), void *data)
{
    return epoch_shrd_defer(&r->epoch, fn, data);
}

bool region_shrd_defer_free(struct ilka_region *r, ilka_off_t off, size_t len, size_t area)
{
    return epoch_shrd_defer_free(&r->epoch, off, len, area);
}

static void region_shrd_world_stop(struct ilka_region *r)
{
    epoch_shrd_world_stop(&r->epoch);
}

static void region_shrd_world_resume(struct ilka_region *r)
{
    epoch_shrd_world_resume(&r->epoch);
}
