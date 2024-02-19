/* -*- mode: C; c-basic-offset: 4 -*- */
/* ex: set shiftwidth=4 tabstop=4 expandtab: */
/*
 * Copyright (c) 2008-2014, Georgia Tech Research Corporation
 * Copyright (c) 2015, Rice University
 * All rights reserved.
 *
 * Author(s): Neil T. Dantam <ntd@rice.edu>
 *            Kim Boendergaard Poulsen <kibo@prevas.dk>
 * Georgia Tech Humanoid Robotics Lab
 * Under Direction of Prof. Mike Stilman <mstilman@cc.gatech.edu>
 *
 *
 * This file is provided under the following "BSD-style" License:
 *
 *
 *   Redistribution and use in source and binary forms, with or
 *   without modification, are permitted provided that the following
 *   conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *
 *   * Neither the name of the copyright holder the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *   CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *   MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 *   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 *   USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 *   AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *   ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *   POSSIBILITY OF SUCH DAMAGE.
 *
 */

/** \file impl_generic.h
 *
 *  \brief This file contains definitions used by both the POSIX and
 *         Linux kernel implementations.
 *
 *  \author Neil T. Dantam
 */

#ifndef ACH_IMPL_H
#define ACH_IMPL_H


/** Lock the channel for reading.
 *
 * \param[in] timeout In user-mode this is an absolute timeout given as a const struct timespec*.  In
 *                    kernel mode, it is a relative timeout given as a const struct timespec64*.
 */
static enum ach_status ACH_WARN_UNUSED
rdlock( ach_channel_t *chan, int wait, const void* timeout );

/** Lock the channel for writing */
static enum ach_status ACH_WARN_UNUSED
wrlock( ach_channel_t *chan );

/** Unlock the channel from a read lock */
static enum ach_status ACH_WARN_UNUSED
unrdlock(struct ach_header *shm);

/** Unlock the channel from a write lock lock */
static enum ach_status ACH_WARN_UNUSED
unwrlock(struct ach_header *shm);


static size_t oldest_index_i( ach_header_t *shm ) {
    return (shm->index_head + shm->index_free)%shm->index_cnt;
}

static size_t last_index_i( ach_header_t *shm ) {
    return (shm->index_head + shm->index_cnt -1)%shm->index_cnt;
}

static enum ach_status ACH_WARN_UNUSED
ach_flush_impl( ach_channel_t *chan )
{
    ach_header_t *shm = chan->shm;
    enum ach_status r = rdlock(chan, 0,  NULL);
    if( ACH_OK != r ) return r;

    chan->seq_num = shm->last_seq;
    chan->next_index = shm->index_head;
    return unrdlock(shm);
}

static enum ach_status ACH_WARN_UNUSED
check_guards( ach_header_t *shm )
{
    if( ACH_SHM_MAGIC_NUM != shm->magic ||
        ACH_SHM_GUARD_HEADER_NUM != *ACH_SHM_GUARD_HEADER(shm) ||
        ACH_SHM_GUARD_INDEX_NUM != *ACH_SHM_GUARD_INDEX(shm) ||
        ACH_SHM_GUARD_DATA_NUM != *ACH_SHM_GUARD_DATA(shm)  )
    {
        ACH_ERRF("ach corrupt: Invalid guard bytes\n");
        return ACH_CORRUPT;
    } else {
        return ACH_OK;
    }
}

static void free_index(ach_header_t *shm, size_t i )
{
    ach_index_t *index_ar = ACH_SHM_INDEX(shm);

#ifdef ACH_POSIX
    assert( index_ar[i].seq_num ); /* only free used indices */
    assert( index_ar[i].size );    /* must have some data */
    assert( shm->index_free < shm->index_cnt ); /* must be some used index */
#endif

    shm->data_free += index_ar[i].size;
    shm->index_free ++;
    memset( &index_ar[i], 0, sizeof( ach_index_t ) );
}


static size_t ach_create_len( size_t frame_cnt, size_t frame_size )
{
    return sizeof(struct ach_header) +
        frame_cnt * sizeof(ach_index_t) +
        frame_cnt * frame_size + 3 * sizeof(uint64_t);
}

static void ach_create_counts( ach_header_t *shm, const char *name, size_t frame_cnt, size_t frame_size )
{

    /* initialize counts */
    shm->index_cnt = frame_cnt;
    shm->index_head = 0;
    shm->index_free = frame_cnt;
    shm->data_head = 0;
    shm->data_free = frame_cnt * frame_size;
    shm->data_size = frame_cnt * frame_size;

    /* magic numbers */
    *ACH_SHM_GUARD_HEADER(shm) = ACH_SHM_GUARD_HEADER_NUM;
    *ACH_SHM_GUARD_INDEX(shm) = ACH_SHM_GUARD_INDEX_NUM;
    *ACH_SHM_GUARD_DATA(shm) = ACH_SHM_GUARD_DATA_NUM;
    shm->magic = ACH_SHM_MAGIC_NUM;

    /* copy name */
    strncpy( shm->name, name, ACH_CHAN_NAME_MAX );
    shm->name[ACH_CHAN_NAME_MAX] = '\0';
}

/** Copies frame pointed to by index entry at index_offset.
 *
 *   \pre hold read lock on the channel
 *
 *   \post on success, transfer is called. seq_num and next_index fields
 *   are incremented. The variable pointed to by frame_size holds the
 *   frame size.
*/
static enum ach_status ACH_WARN_UNUSED
ach_xget_from_offset(ach_channel_t * chan, size_t index_offset,
                     ach_get_fun transfer, void *cx, void **pobj,
                     size_t * frame_size)
{
    struct ach_header *shm;
    ach_index_t *idx;

    shm = chan->shm;
    idx = ACH_SHM_INDEX(shm) + index_offset;

    if (idx->offset >= shm->data_size ) {
        ACH_ERRF("ach bug: overflow data array on ach_get()\n");
        return ACH_BUG;
    }

    /* Is there any possibility to overflow seq_num? Probably not */
    if (chan->seq_num > idx->seq_num) {
        /* ACH_ERRF("ach bug: seq_num mismatch, chan: %llu, idx: %llu, shm: %llu\n", */
        /*          chan->seq_num, idx->seq_num, shm->last_seq); */
        return ACH_BUG;
    }


    if (idx->offset + idx->size > shm->data_size) {
        ACH_ERRF("ach corrupt: frame extends past data array, "
                 " offset: %lu, size: %lu, data size: %lu\n",
                 idx->offset, idx->size, shm->data_size
            );
        return ACH_CORRUPT;
    }

    /* good to copy */
    {
        enum ach_status r;
        unsigned char *data_buf = ACH_SHM_DATA(shm);
        *frame_size = idx->size;
        r = transfer(cx, pobj, data_buf + idx->offset, idx->size);
        if (ACH_OK == r) {
            chan->seq_num = idx->seq_num;
            chan->next_index = (index_offset + 1) % shm->index_cnt;
        }
        return r;
    }
}

/** Pull a message from the channel.
 *
 *  \pre chan has been opened with ach_open()
 *
 *  Note that transfer() is called while holding the channel lock.
 *  Expensive computation should thus be avoided during this call.
 *
 *  We could expose this function to reduce copying.  However, it
 *  would be complicated and risky for kernel channels, requiring the
 *  data buffer be mapped into userspace.  It could also deadlock
 *  usermode channels if the transfer function exits the program.
 *
 *  \param [in,out] chan The previously opened channel handle
 *
 *  \param [in] transfer Function to transfer data out of the channel
 *
 *  \param [in,out] cx Context argument to transfer
 *
 *  \param [in,out] pobj Pointer to object pointer
 *
 *  \param [out] frame_size The number of bytes occupied by the frame in the channel
 *
 *  \param [in] timeout A timeout if ACH_O_WAIT is specified.  Take
 *              care that abstime is given in the correct clock.  The
 *              default is defined by ACH_DEFAULT_CLOCK.  In user-mode
 *              this is an absolute timeout.  In kernel mode, it is a
 *              relative timeout.
 *
 *  \param[in] options Option flags
 *
 *  \return ACH_OK on success.
 */
static enum ach_status ACH_WARN_UNUSED
ach_xget(ach_channel_t * chan, ach_get_fun transfer, void *cx, void **pobj,
         size_t * frame_size,
         const void *timeout,
         int options )
{
    struct ach_header *shm = chan->shm;
    bool missed_frame = false;
    enum ach_status retval = ACH_BUG;
    const bool o_wait = options & ACH_O_WAIT;
    const bool o_last = options & ACH_O_LAST;
    const bool o_copy =  options & ACH_O_COPY;
    enum ach_status r;

    /* Check guard bytes */
    if( ACH_OK != (r=check_guards(shm)) ) return r;

    /* Take read lock */
    if ( ACH_OK != (r=rdlock(chan, o_wait, timeout)) ) return r;

    /* get the data */
    if ((chan->seq_num == shm->last_seq && !o_copy) || 0 == shm->last_seq) {
        /* no entries */
        retval = ACH_STALE_FRAMES;
    } else {
        /* Compute the index to read */
        size_t read_index;
        ach_index_t *index_ar = ACH_SHM_INDEX(shm);
        if (o_last) {
            /* normal case, get last */
            /* assert(!o_wait); */
            read_index = last_index_i(shm);
        } else if (!o_last &&
                   index_ar[chan->next_index].seq_num ==
                   chan->seq_num + 1) {
            /* normal case, get next */
            read_index = chan->next_index;
        } else {
            /* exception case, figure out which frame */
            if (chan->seq_num == shm->last_seq) {
                /* copy last */
                /* assert(o_copy); */
                read_index = last_index_i(shm);
            } else {
                /* copy oldest */
                read_index = oldest_index_i(shm);
            }
        }

        if (index_ar[read_index].seq_num > chan->seq_num + 1) {
            missed_frame = true;
        }

        /* read from the index */
        retval =
            ach_xget_from_offset(chan, read_index, transfer, cx, pobj,
                                 frame_size);

        /* assert( index_ar[read_index].seq_num > 0 ); */
    }

    /* relase read lock */
    if ( ACH_OK != (r=unrdlock(shm)) ) return r;

    return (ACH_OK == retval && missed_frame) ? ACH_MISSED_FRAME : retval;
}


/* #define ACH_XPUT_ASSERT(cond)						\ */
/*      {								\ */
/*              if (! (cond) ) {					\ */
/*                      printk(KERN_ERR "Logic error in ACH\n");	\ */
/*                      return ACH_BUG;					\ */
/*              }							\ */
/*      } */


/** Writes a new message in the channel.
 *
 *  \pre chan has been opened with ach_open() and is large enough
 *  to hold the message.
 *
 *  Note that transfer() is called while holding the channel lock.
 *  Expensive computation should thus be avoided during this call.
 *
 *  \param [in,out] chan The channel to write to
 *  \param [in] transfer Function to transfer data into the channel
 *  \param [in,out] cx Context argument to transfer
 *  \param [in] obj Source object passed to transfer()
 *  \param [in] dst_size Number of bytes needed in the channel to hold obj
 *
 *  \return ACH_OK on success. If the channel is too small to hold
 *  the frame, returns ACH_OVERFLOW.
 */
static enum ach_status ACH_WARN_UNUSED
ach_xput( ach_channel_t *chan,
          ach_put_fun transfer, void *cx, const void *obj, size_t len )
{

    struct ach_header *shm = chan->shm;
    ach_index_t *index_ar;
    unsigned char *data_ar;
    ach_index_t *idx;

    if( 0 == len || NULL == transfer || NULL == chan->shm ) {
        return ACH_EINVAL;
    }

    /* Check guard bytes */
    {
        enum ach_status r = check_guards(shm);
        if( ACH_OK != r ) return r;
    }

    if( shm->data_size < len ) {
        return ACH_OVERFLOW;
    }

    index_ar = ACH_SHM_INDEX(shm);
    data_ar = ACH_SHM_DATA(shm);

    if( len > shm->data_size ) return ACH_OVERFLOW;

    /* take write lock */
    {
        enum ach_status r = wrlock(chan);
        if( ACH_OK != r) return r;
    }

    /* find next index entry */
    idx = index_ar + shm->index_head;

    /* clear entry used by index */
    if( 0 == shm->index_free ) { free_index(shm,shm->index_head); }
    /* else { assert(0== index_ar[shm->index_head].seq_num);} */

    /* assert( shm->index_free > 0 ); */

    /* Avoid wraparound */
    if( shm->data_size - shm->data_head < len ) {
        size_t i;
        /* clear to end of array */
        /* assert( 0 == index_ar[shm->index_head].offset ); */
        for(i = (shm->index_head + shm->index_free) % shm->index_cnt;
            index_ar[i].offset > shm->data_head;
            i = (i + 1) % shm->index_cnt)
        {
            /* assert( i != shm->index_head ); */
            free_index(shm,i);
        }
        /* Set counts to beginning of array */
        if( i == shm->index_head ) {
            shm->data_free = shm->data_size;
        } else {
            shm->data_free = index_ar[oldest_index_i(shm)].offset;
        }
        shm->data_head = 0;
    }

    /* clear overlapping entries */
    {
        size_t i;
        for(i = (shm->index_head + shm->index_free) % shm->index_cnt;
            shm->data_free < len;
            i = (i + 1) % shm->index_cnt)
        {
            if( i == shm->index_head ) {
                shm->data_free = shm->data_size;
            } else {
                free_index(shm,i);
            }
        }
    }

    /* assert( shm->data_free >= len ); */

    if( shm->data_size - shm->data_head < len ) {
        enum ach_status r2 = unwrlock( shm );
        if( r2 != ACH_OK ) {
            ACH_ERRF("ach bug: another error on unwrlock()");
        }
        return ACH_BUG;
    }

    /* transfer */
    {
        enum ach_status r = transfer(cx, data_ar + shm->data_head, obj);
        if( ACH_OK != r ) {
            enum ach_status r2 = unwrlock( shm );
            if( r2 != ACH_OK ) {
                ACH_ERRF("ach bug: another error on unwrlock()");
            }
            return r;
        }
    }

    /* modify counts */
    shm->last_seq++;
    idx->seq_num = shm->last_seq;
    idx->size = len;
    idx->offset = shm->data_head;

    shm->data_head = (shm->data_head + len) % shm->data_size;
    shm->data_free -= len;
    shm->index_head = (shm->index_head + 1) % shm->index_cnt;
    shm->index_free --;

    /* assert( shm->index_free <= shm->index_cnt ); */
    /* assert( shm->data_free <= shm->data_size ); */
    /* assert( shm->last_seq > 0 ); */

    /* release write lock */
    return unwrlock( shm );
}


#endif /* ACH_IMPL_H */
