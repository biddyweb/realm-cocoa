#include <iostream>
#include <iomanip>

#include <tightdb/column_table.hpp>

using namespace std;
using namespace tightdb;
using namespace tightdb::util;


void ColumnSubtableParent::update_from_parent(size_t old_baseline) TIGHTDB_NOEXCEPT
{
    if (!m_array->update_from_parent(old_baseline))
        return;
    m_subtable_map.update_from_parent(old_baseline);
}


Table* ColumnSubtableParent::get_subtable_ptr(size_t subtable_ndx)
{
    TIGHTDB_ASSERT(subtable_ndx < size());
    if (Table* subtable = m_subtable_map.find(subtable_ndx))
        return subtable;

    typedef _impl::TableFriend tf;
    ref_type top_ref = get_as_ref(subtable_ndx);
    Allocator& alloc = get_alloc();
    ColumnSubtableParent* parent = this;
    UniquePtr<Table> subtable(tf::create_ref_counted(alloc, top_ref, parent,
                                                     subtable_ndx)); // Throws
    // FIXME: Note that if the following map insertion fails, then the
    // destructor of the newly created child will call
    // ColumnSubtableParent::child_accessor_destroyed() with a pointer that is
    // not in the map. Fortunatly, that situation is properly handled.
    bool was_empty = m_subtable_map.empty();
    m_subtable_map.add(subtable_ndx, subtable.get()); // Throws
    if (was_empty && m_table)
        tf::bind_ref(*m_table);
    return subtable.release();
}


Table* ColumnTable::get_subtable_ptr(size_t subtable_ndx)
{
    TIGHTDB_ASSERT(subtable_ndx < size());
    if (Table* subtable = m_subtable_map.find(subtable_ndx))
        return subtable;

    typedef _impl::TableFriend tf;
    const Spec* spec = tf::get_spec(*m_table);
    size_t subspec_ndx = get_subspec_ndx();
    ConstSubspecRef shared_subspec = spec->get_subspec_by_ndx(subspec_ndx);
    ref_type columns_ref = get_as_ref(subtable_ndx);
    ColumnTable* parent = this;
    UniquePtr<Table> subtable(tf::create_ref_counted(shared_subspec, columns_ref,
                                                     parent, subtable_ndx)); // Throws
    // FIXME: Note that if the following map insertion fails, then the
    // destructor of the newly created child will call
    // ColumnSubtableParent::child_accessor_destroyed() with a pointer that is
    // not in the map. Fortunatly, that situation is properly handled.
    bool was_empty = m_subtable_map.empty();
    m_subtable_map.add(subtable_ndx, subtable.get()); // Throws
    if (was_empty && m_table)
        tf::bind_ref(*m_table);
    return subtable.release();
}


void ColumnSubtableParent::child_accessor_destroyed(Table* child) TIGHTDB_NOEXCEPT
{
    // This function must be able to operate with only the Minimal Accessor
    // Hierarchy Consistency Guarantee. This means, in particular, that it
    // cannot access the underlying array structure.

    // Note that due to the possibility of a failure during child creation, it
    // is possible that the calling child is not in the map.

    bool last_entry_removed = m_subtable_map.remove(child);

    // Note that this column instance may be destroyed upon return
    // from Table::unbind_ref(), i.e., a so-called suicide is
    // possible.
    typedef _impl::TableFriend tf;
    if (last_entry_removed && m_table)
        tf::unbind_ref(*m_table);
}


Table* ColumnSubtableParent::get_parent_table(size_t* column_ndx_out) const TIGHTDB_NOEXCEPT
{
    if (column_ndx_out)
        *column_ndx_out = m_column_ndx;
    return m_table;
}


Table* ColumnSubtableParent::SubtableMap::find(size_t subtable_ndx) const TIGHTDB_NOEXCEPT
{
    typedef entries::const_iterator iter;
    iter end = m_entries.end();
    for (iter i = m_entries.begin(); i != end; ++i)
        if (i->m_subtable_ndx == subtable_ndx)
            return i->m_table;
    return 0;
}


bool ColumnSubtableParent::SubtableMap::detach_and_remove_all() TIGHTDB_NOEXCEPT
{
    typedef entries::const_iterator iter;
    iter end = m_entries.end();
    for (iter i = m_entries.begin(); i != end; ++i) {
        // Must hold a counted reference while detaching
        TableRef table(i->m_table);
        typedef _impl::TableFriend tf;
        tf::detach(*table);
    }
    bool was_empty = m_entries.empty();
    m_entries.clear();
    return !was_empty;
}


bool ColumnSubtableParent::SubtableMap::detach_and_remove(size_t subtable_ndx) TIGHTDB_NOEXCEPT
{
    typedef entries::iterator iter;
    iter i = m_entries.begin(), end = m_entries.end();
    for (;;) {
        if (i == end)
            return false;
        if (i->m_subtable_ndx == subtable_ndx)
            break;
        ++i;
    }

    // Must hold a counted reference while detaching
    TableRef table(i->m_table);
    typedef _impl::TableFriend tf;
    tf::detach(*table);

    *i = *--end; // Move last over
    m_entries.pop_back();
    return m_entries.empty();
}


bool ColumnSubtableParent::SubtableMap::remove(Table* subtable) TIGHTDB_NOEXCEPT
{
    typedef entries::iterator iter;
    iter i = m_entries.begin(), end = m_entries.end();
    for (;;) {
        if (i == end)
            return false;
        if (i->m_table == subtable)
            break;
        ++i;
    }
    *i = *--end; // Move last over
    m_entries.pop_back();
    return m_entries.empty();
}


void ColumnSubtableParent::SubtableMap::update_from_parent(size_t old_baseline)
    const TIGHTDB_NOEXCEPT
{
    typedef _impl::TableFriend tf;
    typedef entries::const_iterator iter;
    iter end = m_entries.end();
    for (iter i = m_entries.begin(); i != end; ++i)
        tf::update_from_parent(*i->m_table, old_baseline);
}


void ColumnSubtableParent::SubtableMap::adj_insert_rows(size_t row_ndx, size_t num_rows)
    TIGHTDB_NOEXCEPT
{
    typedef entries::iterator iter;
    iter end = m_entries.end();
    for (iter i = m_entries.begin(); i != end; ++i) {
        if (i->m_subtable_ndx >= row_ndx)
            i->m_subtable_ndx += num_rows;
    }
}


bool ColumnSubtableParent::SubtableMap::adj_erase_row(size_t row_ndx) TIGHTDB_NOEXCEPT
{
    typedef entries::iterator iter;
    iter end = m_entries.end();
    iter i = end;
    for (iter j = m_entries.begin(); j != end; ++j) {
        if (j->m_subtable_ndx > row_ndx) {
            --j->m_subtable_ndx;
        }
        else if (j->m_subtable_ndx == row_ndx) {
            TIGHTDB_ASSERT(i == end); // Subtable accessors are unique
            i = j;
        }
    }
    if (i == end)
        return false; // Not found, so nothing changed

    // Must hold a counted reference while detaching
    TableRef table(i->m_table);
    typedef _impl::TableFriend tf;
    tf::detach(*table);

    *i = *--end; // Move last over
    m_entries.pop_back();
    return m_entries.empty();
}


bool ColumnSubtableParent::SubtableMap::adj_move_last_over(size_t target_row_ndx,
                                                           size_t last_row_ndx) TIGHTDB_NOEXCEPT
{
    // Search for either index in a tight loop for speed
    bool last_seen = false;
    typedef entries::iterator iter;
    iter i = m_entries.begin(), end = m_entries.end();
    for (;;) {
        if (i == end)
            return false;
        if (i->m_subtable_ndx == target_row_ndx)
            goto target;
        if (i->m_subtable_ndx == last_row_ndx)
            break;
        ++i;
    }

    // Move subtable accessor at `last_row_ndx`, then look for `target_row_ndx`
    i->m_subtable_ndx = target_row_ndx;
    for (;;) {
        ++i;
        if (i == end)
            return false;
        if (i->m_subtable_ndx == target_row_ndx)
            break;
    }
    last_seen = true;

    // Detach and remove original subtable accessor at `target_row_ndx`, then
    // look for `last_row_ndx
  target:
    {
        // Must hold a counted reference while detaching
        TableRef table(i->m_table);
        typedef _impl::TableFriend tf;
        tf::detach(*table);
        // Delete entry by moving last over (faster and avoids invalidating
        // iterators)
        *i = *--end;
        m_entries.pop_back();
    }
    if (!last_seen) {
        for (;;) {
            if (i == end)
                goto check_empty;
            if (i->m_subtable_ndx == last_row_ndx)
                break;
            ++i;
        }
        i->m_subtable_ndx = target_row_ndx;
    }

  check_empty:
    return m_entries.empty();
}


void ColumnSubtableParent::SubtableMap::
update_accessors(const size_t* col_path_begin, const size_t* col_path_end,
                 _impl::TableFriend::AccessorUpdater& updater)
{
    typedef entries::const_iterator iter;
    iter end = m_entries.end();
    for (iter i = m_entries.begin(); i != end; ++i) {
        // Must hold a counted reference while updating
        TableRef table(i->m_table);
        typedef _impl::TableFriend tf;
        tf::update_accessors(*table, col_path_begin, col_path_end, updater);
    }
}


#ifdef TIGHTDB_ENABLE_REPLICATION

void ColumnSubtableParent::SubtableMap::recursive_mark_dirty() TIGHTDB_NOEXCEPT
{
    typedef entries::const_iterator iter;
    iter end = m_entries.end();
    for (iter i = m_entries.begin(); i != end; ++i) {
        TableRef table(i->m_table);
        typedef _impl::TableFriend tf;
        tf::recursive_mark_dirty(*table);
    }
}

void ColumnSubtableParent::SubtableMap::refresh_after_advance_transact(size_t spec_ndx_in_parent)
{
    typedef entries::const_iterator iter;
    iter end = m_entries.end();
    for (iter i = m_entries.begin(); i != end; ++i) {
        // Must hold a counted reference while refreshing
        TableRef table(i->m_table);
        typedef _impl::TableFriend tf;
        tf::refresh_after_advance_transact(*table, i->m_subtable_ndx, spec_ndx_in_parent);
    }
}

#endif // TIGHTDB_ENABLE_REPLICATION


#ifdef TIGHTDB_DEBUG

pair<ref_type, size_t> ColumnSubtableParent::get_to_dot_parent(size_t ndx_in_parent) const
{
    pair<MemRef, size_t> p = m_array->get_bptree_leaf(ndx_in_parent);
    return make_pair(p.first.m_ref, p.second);
}

#endif


size_t ColumnTable::get_subtable_size(size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(ndx < size());

    ref_type columns_ref = get_as_ref(ndx);
    if (columns_ref == 0)
        return 0;

    typedef _impl::TableFriend tf;
    size_t subspec_ndx = get_subspec_ndx();
    Spec* spec = tf::get_spec(*m_table);
    ref_type subspec_ref = spec->get_subspec_ref(subspec_ndx);
    Allocator& alloc = spec->get_alloc();
    return tf::get_size_from_ref(subspec_ref, columns_ref, alloc);
}


void ColumnTable::add()
{
    add(0); // Null-pointer indicates empty table
}


void ColumnTable::insert(size_t ndx)
{
    insert(ndx, 0); // Null-pointer indicates empty table
}


void ColumnTable::insert(size_t ndx, const Table* subtable)
{
    TIGHTDB_ASSERT(ndx <= size());
    detach_subtable_accessors();

    ref_type columns_ref = 0;
    if (subtable)
        columns_ref = clone_table_columns(subtable); // Throws

    Column::insert(ndx, columns_ref); // Throws
}


void ColumnTable::set(size_t ndx, const Table* subtable)
{
    TIGHTDB_ASSERT(ndx < size());
    detach_subtable_accessors();
    destroy_subtable(ndx);

    ref_type columns_ref = 0;
    if (subtable)
        columns_ref = clone_table_columns(subtable);

    Column::set(ndx, columns_ref);
}


void ColumnTable::erase(size_t ndx, bool is_last)
{
    TIGHTDB_ASSERT(ndx < size());
    detach_subtable_accessors();
    destroy_subtable(ndx);
    Column::erase(ndx, is_last);
}


void ColumnTable::clear()
{
    detach_subtable_accessors();
    Column::clear();
    // FIXME: This one is needed because Column::clear() forgets about
    // the leaf type. A better solution should probably be found.
    m_array->set_type(Array::type_HasRefs);
}


void ColumnTable::move_last_over(size_t ndx)
{
    TIGHTDB_ASSERT(ndx+1 < size());
    detach_subtable_accessors();
    destroy_subtable(ndx);

    size_t last_ndx = size() - 1;
    int_fast64_t v = get(last_ndx);
    Column::set(ndx, v);

    bool is_last = true;
    Column::erase(last_ndx, is_last);
}


void ColumnTable::destroy_subtable(size_t ndx)
{
    ref_type columns_ref = get_as_ref(ndx);
    if (columns_ref == 0)
        return; // It was never created

    // Delete sub-tree
    Allocator& alloc = get_alloc();
    Array columns(columns_ref, 0, 0, alloc);
    columns.destroy_deep();
}


bool ColumnTable::compare_table(const ColumnTable& c) const
{
    size_t n = size();
    if (c.size() != n)
        return false;
    for (size_t i = 0; i != n; ++i) {
        ConstTableRef t1 = get_subtable_ptr(i)->get_table_ref(); // Throws
        ConstTableRef t2 = c.get_subtable_ptr(i)->get_table_ref(); // throws
        if (!compare_subtable_rows(*t1, *t2))
            return false;
    }
    return true;
}


void ColumnTable::do_detach_subtable_accessors() TIGHTDB_NOEXCEPT
{
    detach_subtable_accessors();
}


#ifdef TIGHTDB_DEBUG

void ColumnTable::Verify() const
{
    Column::Verify();

    // Verify each sub-table
    size_t n = size();
    for (size_t i = 0; i != n; ++i) {
        // We want to verify any cached table accessors so we do not
        // want to skip null refs here.
        ConstTableRef subtable = get_subtable_ptr(i)->get_table_ref();
        subtable->Verify();
    }
}

void ColumnTable::to_dot(ostream& out, StringData title) const
{
    ref_type ref = m_array->get_ref();
    out << "subgraph cluster_subtable_column" << ref << " {" << endl;
    out << " label = \"Subtable column";
    if (title.size() != 0)
        out << "\\n'" << title << "'";
    out << "\";" << endl;
    tree_to_dot(out);
    out << "}" << endl;

    size_t n = size();
    for (size_t i = 0; i != n; ++i) {
        if (get_as_ref(i) == 0)
            continue;
        ConstTableRef subtable = get_subtable_ptr(i)->get_table_ref();
        subtable->to_dot(out);
    }
}

namespace {

void leaf_dumper(MemRef mem, Allocator& alloc, ostream& out, int level)
{
    Array leaf(mem, 0, 0, alloc);
    int indent = level * 2;
    out << setw(indent) << "" << "Subtable leaf (size: "<<leaf.size()<<")\n";
}

} // anonymous namespace

void ColumnTable::dump_node_structure(ostream& out, int level) const
{
    m_array->dump_bptree_structure(out, level, &leaf_dumper);
}

#endif // TIGHTDB_DEBUG