/* region_private.c
   Rémi Attab (remi.attab@gmail.com), 27 Feb 2016
   FreeBSD-style copyright and disclaimer apply
*/


static bool region_priv_save(struct ilka_region *r);

// -----------------------------------------------------------------------------
// region
// -----------------------------------------------------------------------------

static struct ilka_region *
region_priv_open(const char *file, struct ilka_options *options)
{
    journal_recover(file);

    struct ilka_region *r = region_alloc(file, options);
    if (!r) return NULL;

    if ((r->fd = file_open(file, &r->options)) == -1) goto fail_open;
    if ((r->len = file_grow(r->fd, ILKA_PAGE_SIZE)) == -1UL) goto fail_grow;
    if (!mmap_init(&r->mmap, r->fd, r->len, &r->options)) goto fail_mmap;
    if (!persist_init(&r->persist, r, r->file)) goto fail_persist;

    const struct meta *meta = meta_init(r);
    if (!meta) goto fail_meta;

    if (!alloc_init(&r->alloc, r, &r->options, meta->alloc)) goto fail_alloc;
    if (!epoch_priv_init(&r->epoch.priv, r, &r->options)) goto fail_epoch;
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

static bool region_priv_close(struct ilka_region *r)
{
    if (!region_priv_save(r)) return false;

    epoch_priv_close(&r->epoch.priv);
    persist_close(&r->persist);

    if (!mmap_close(&r->mmap)) return false;
    if (!file_close(r->fd)) return false;
    free(r);

    return true;
}

static bool region_priv_rm(struct ilka_region *r)
{
    const char *file = r->file;
    if (!ilka_close(r)) return false;
    return file_rm(file);
}


// -----------------------------------------------------------------------------
// persist
// -----------------------------------------------------------------------------

static bool region_priv_save(struct ilka_region *r)
{
    return persist_save(&r->persist);
}

static void region_priv_mark(struct ilka_region *r, ilka_off_t off, size_t len)
{
    persist_mark(&r->persist, off, len);
}


// -----------------------------------------------------------------------------
// epoch
// -----------------------------------------------------------------------------

static bool region_priv_enter(struct ilka_region *r)
{
    return epoch_priv_enter(&r->epoch.priv);
}

static void region_priv_exit(struct ilka_region *r)
{
    epoch_priv_exit(&r->epoch.priv);
}

static bool region_priv_defer(struct ilka_region *r, void (*fn) (void *), void *data)
{
    return epoch_priv_defer(&r->epoch.priv, fn, data);
}

bool region_priv_defer_free(struct ilka_region *r, ilka_off_t off, size_t len, size_t area)
{
    return epoch_priv_defer_free(&r->epoch.priv, off, len, area);
}

static void region_priv_world_stop(struct ilka_region *r)
{
    epoch_priv_world_stop(&r->epoch.priv);
    mmap_coalesce(&r->mmap);
}

static void region_priv_world_resume(struct ilka_region *r)
{
    epoch_priv_world_resume(&r->epoch.priv);
}
