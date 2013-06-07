/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 2012,2013, by the GROMACS development team, led by
 * David van der Spoel, Berk Hess, Erik Lindahl, and including many
 * others, as listed in the AUTHORS file in the top-level source
 * directory and at http://www.gromacs.org.
 *
 * GROMACS is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * GROMACS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GROMACS; if not, see
 * http://www.gnu.org/licenses, or write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *
 * If you want to redistribute modifications to GROMACS, please
 * consider that scientific software is very special. Version
 * control is crucial - bugs must be traceable. We will be happy to
 * consider code for inclusion in the official distribution, but
 * derived work must not be called official GROMACS. Details are found
 * in the README & COPYING files - if they are missing, get the
 * official version at http://www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the research papers on the package. Check out http://www.gromacs.org.
 */
/*! \internal \file
 * \brief
 * Implements classes in datastorage.h and paralleloptions.h.
 *
 * \author Teemu Murtola <teemu.murtola@gmail.com>
 * \ingroup module_analysisdata
 */
#include "datastorage.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <vector>

#include "gromacs/analysisdata/abstractdata.h"
#include "gromacs/analysisdata/dataframe.h"
#include "gromacs/analysisdata/paralleloptions.h"
#include "gromacs/utility/exceptions.h"
#include "gromacs/utility/gmxassert.h"
#include "gromacs/utility/uniqueptr.h"

namespace gmx
{

/********************************************************************
 * AnalysisDataParallelOptions
 */

AnalysisDataParallelOptions::AnalysisDataParallelOptions()
    : parallelizationFactor_(1)
{
}


AnalysisDataParallelOptions::AnalysisDataParallelOptions(int parallelizationFactor)
    : parallelizationFactor_(parallelizationFactor)
{
    GMX_RELEASE_ASSERT(parallelizationFactor >= 1,
                       "Invalid parallelization factor");
}


/********************************************************************
 * AnalysisDataStorage::Impl declaration
 */

namespace internal
{
//! Smart pointer type for managing a storage frame builder.
typedef gmx_unique_ptr<AnalysisDataStorageFrame>::type
    AnalysisDataFrameBuilderPointer;
}   // namespace internal

/*! \internal \brief
 * Private implementation class for AnalysisDataStorage.
 *
 * \ingroup module_analysisdata
 */
class AnalysisDataStorage::Impl
{
    public:
        //! Short-hand for the internal frame data type.
        typedef internal::AnalysisDataStorageFrameData FrameData;
        //! Smart pointer type for managing a stored frame.
        typedef gmx_unique_ptr<FrameData>::type FramePointer;
        //! Short-hand for a smart pointer type to a storage frame builder.
        typedef internal::AnalysisDataFrameBuilderPointer FrameBuilderPointer;

        //! Shorthand for a list of data frames that are currently stored.
        typedef std::vector<FramePointer> FrameList;
        //! Shorthand for a list of currently unused storage frame builders.
        typedef std::vector<FrameBuilderPointer> FrameBuilderList;

        Impl();

        //! Returns whether the storage is set to use multipoint data.
        bool isMultipoint() const;
        /*! \brief
         * Whether storage of all frames has been requested.
         *
         * Storage of all frames also works as expected if \a storageLimit_ is
         * used in comparisons directly, but this method should be used to
         * check how to manage \a frames_.
         */
        bool storeAll() const
        {
            return storageLimit_ == std::numeric_limits<int>::max();
        }
        //! Returns the index of the oldest frame that may be currently stored.
        int firstStoredIndex() const;
        /*! \brief
         * Computes index into \a frames_ for accessing frame \p index.
         *
         * \param[in]  index  Zero-based frame index.
         * \retval  -1 if \p index is not available in \a frames_.
         *
         * Does not throw.
         */
        int computeStorageLocation(int index) const;

        /*! \brief
         * Computes an index into \a frames_ that is one past the last frame
         * stored.
         *
         * Does not throw.
         */
        size_t endStorageLocation() const;

        /*! \brief
         * Extends \a frames_ to a new size.
         *
         * \throws std::bad_alloc if out of memory.
         */
        void extendBuffer(size_t newSize);
        /*! \brief
         * Remove oldest frame from the storage to make space for a new one.
         *
         * Increments \a firstFrameLocation_ and reinitializes the frame that
         * was made unavailable by this operation.
         *
         * Does not throw.
         *
         * \see frames_
         */
        void rotateBuffer();

        /*! \brief
         * Returns a frame builder object for use with a new frame.
         *
         * \throws std::bad_alloc if out of memory.
         */
        FrameBuilderPointer getFrameBuilder();

        /*! \brief
         * Returns whether notifications should be immediately fired.
         *
         * This is used to optimize multipoint handling for non-parallel cases,
         * where it is not necessary to store even a single frame.
         *
         * Does not throw.
         */
        bool shouldNotifyImmediately() const
        {
            return isMultipoint() && storageLimit_ == 0 && pendingLimit_ == 1;
        }
        /*! \brief
         * Calls notification method in \a data_.
         *
         * \throws    unspecified  Any exception thrown by
         *      AbstractAnalysisData::notifyPointsAdd().
         */
        void notifyPointSet(const AnalysisDataPointSetRef &points);
        /*! \brief
         * Calls notification methods for new frames.
         *
         * \param[in] firstLocation  First frame to consider.
         * \throws    unspecified  Any exception thrown by frame notification
         *      methods in AbstractAnalysisData.
         *
         * Notifies \a data_ of new frames (from \p firstLocation and after
         * that) if all previous frames have already been notified.
         * Also rotates the \a frames_ buffer as necessary.
         */
        void notifyNextFrames(size_t firstLocation);
        //! Implementation for AnalysisDataStorage::finishFrame().
        void finishFrame(int index);


        //! Data object to use for notification calls.
        AbstractAnalysisData   *data_;
        /*! \brief
         * Number of past frames that need to be stored.
         *
         * Always non-negative.  If storage of all frames has been requested,
         * this is set to a large number.
         */
        int                     storageLimit_;
        /*! \brief
         * Number of future frames that may need to be started.
         *
         * Should always be at least one.
         *
         * \see AnalysisDataStorage::startFrame()
         */
        int                     pendingLimit_;
        /*! \brief
         * Data frames that are currently stored.
         *
         * If storage of all frames has been requested, this is simply a vector
         * of frames up to the latest frame that has been started.
         * In this case, \a firstFrameLocation_ is always zero.
         *
         * If storage of all frames is not requested, this is a ring buffer of
         * frames of size \c n=storageLimit_+pendingLimit_+1.  If a frame with
         * index \c index is currently stored, its location is
         * \c index%frames_.size().
         * When at most \a storageLimit_ first frames have been finished,
         * this contains storage for the first \c n-1 frames.
         * When more than \a storageLimit_ first frames have been finished,
         * the oldest stored frame is stored in the location
         * \a firstFrameLocation_, and \a storageLimit_ frames starting from
         * this location are the last finished frames.  \a pendingLimit_ frames
         * follow, and some of these may be in progress or finished.
         * There is always one unused frame in the buffer, which is initialized
         * such that when \a firstFrameLocation_ is incremented, it becomes
         * valid.  This makes it easier to rotate the buffer in concurrent
         * access scenarions (which are not yet otherwise implemented).
         */
        FrameList               frames_;
        //! Location of oldest frame in \a frames_.
        size_t                  firstFrameLocation_;
        /*! \brief
         * Currently unused frame builders.
         *
         * The builders are cached to avoid repeatedly allocating memory for
         * them.  Typically, there are as many builders as there are concurrent
         * users of the storage object.  Whenever a frame is started, a builder
         * is pulled from this pool by getFrameBuilder() (a new one is created
         * if none are available), and assigned for that frame.  When that
         * frame is finished, the builder is returned to this pool.
         */
        FrameBuilderList        builders_;
        /*! \brief
         * Index of next frame that will be added to \a frames_.
         *
         * If all frames are not stored, this will be the index of the unused
         * frame (see \a frames_).
         */
        int                     nextIndex_;
};

/********************************************************************
 * AnalysisDataStorageFrameImpl declaration
 */

namespace internal
{

/*! \internal \brief
 * Internal representation for a single stored frame.
 *
 * It is implemented such that the frame header is always valid, i.e.,
 * header().isValid() returns always true.
 *
 * Methods in this class do not throw unless otherwise indicated.
 *
 * \ingroup module_analysisdata
 */
class AnalysisDataStorageFrameData
{
    public:
        //! Shorthand for a iterator into storage value containers.
        typedef std::vector<AnalysisDataValue>::const_iterator ValueIterator;

        //! Indicates what operations have been performed on a frame.
        enum Status
        {
            eMissing,  //!< Frame has not yet been started.
            eStarted,  //!< startFrame() has been called.
            eFinished, //!< finishFrame() has been called.
            eNotified  //!< Appropriate notifications have been sent.
        };

        /*! \brief
         * Create a new storage frame.
         *
         * \param     storageImpl  Storage object this frame belongs to.
         * \param[in] index        Zero-based index for the frame.
         */
        AnalysisDataStorageFrameData(AnalysisDataStorage::Impl *storageImpl,
                                     int                        index);

        //! Whether the frame has been started with startFrame().
        bool isStarted() const { return status_ >= eStarted; }
        //! Whether the frame has been finished with finishFrame().
        bool isFinished() const { return status_ >= eFinished; }
        //! Whether all notifications have been sent.
        bool isNotified() const { return status_ >= eNotified; }
        //! Whether the frame is ready to be available outside the storage.
        bool isAvailable() const { return status_ >= eFinished; }

        //! Marks the frame as notified.
        void markNotified() { status_ = eNotified; }

        //! Returns the storage implementation object.
        AnalysisDataStorage::Impl &storageImpl() const { return storageImpl_; }
        //! Returns the underlying data object (for data dimensionalities etc.).
        const AbstractAnalysisData &baseData() const { return *storageImpl().data_; }

        //! Returns header for the frame.
        const AnalysisDataFrameHeader &header() const { return header_; }
        //! Returns zero-based index of the frame.
        int frameIndex() const { return header().index(); }
        //! Returns the number of point sets for the frame.
        int pointSetCount() const { return pointSets_.size(); }

        //! Clears the frame for reusing as a new frame.
        void clearFrame(int newIndex);
        /*! \brief
         * Initializes the frame during AnalysisDataStorage::startFrame().
         *
         * \param[in] header  Header to use for the new frame.
         * \param[in] builder Builder object to use.
         */
        void startFrame(const AnalysisDataFrameHeader   &header,
                        AnalysisDataFrameBuilderPointer  builder);
        //! Returns the builder for this frame.
        AnalysisDataStorageFrame &builder() const
        {
            GMX_ASSERT(builder_, "Accessing builder for not-in-progress frame");
            return *builder_;
        }
        /*! \brief
         * Adds a new point set to this frame.
         */
        void addPointSet(int dataSetIndex, int firstColumn,
                         ValueIterator begin, ValueIterator end);
        /*! \brief
         * Finalizes the frame during AnalysisDataStorage::finishFrame().
         *
         * \returns The builder object used by the frame, for reusing it for
         *      other frames.
         */
        AnalysisDataFrameBuilderPointer finishFrame(bool bMultipoint);

        //! Returns frame reference to this frame.
        AnalysisDataFrameRef frameReference() const
        {
            return AnalysisDataFrameRef(header_, values_, pointSets_);
        }
        //! Returns point set reference to a given point set.
        AnalysisDataPointSetRef pointSet(int index) const;

    private:
        //! Storage object that contains this frame.
        AnalysisDataStorage::Impl              &storageImpl_;
        //! Header for the frame.
        AnalysisDataFrameHeader                 header_;
        //! Values for the frame.
        std::vector<AnalysisDataValue>          values_;
        //! Information about each point set in the frame.
        std::vector<AnalysisDataPointSetInfo>   pointSets_;
        /*! \brief
         * Builder object for the frame.
         *
         * Non-NULL when the frame is in progress, i.e., has been started but
         * not yet finished.
         */
        AnalysisDataFrameBuilderPointer         builder_;
        //! In what state the frame currently is.
        Status                                  status_;

        GMX_DISALLOW_COPY_AND_ASSIGN(AnalysisDataStorageFrameData);
};

}   // namespace internal

/********************************************************************
 * AnalysisDataStorage::Impl implementation
 */

AnalysisDataStorage::Impl::Impl()
    : data_(NULL),
      storageLimit_(0), pendingLimit_(1), firstFrameLocation_(0), nextIndex_(0)
{
}


bool
AnalysisDataStorage::Impl::isMultipoint() const
{
    GMX_ASSERT(data_ != NULL, "isMultipoint() called too early");
    return data_->isMultipoint();
}


int
AnalysisDataStorage::Impl::firstStoredIndex() const
{
    return frames_[firstFrameLocation_]->frameIndex();
}


int
AnalysisDataStorage::Impl::computeStorageLocation(int index) const
{
    if (index < firstStoredIndex() || index >= nextIndex_)
    {
        return -1;
    }
    return index % frames_.size();
}


size_t
AnalysisDataStorage::Impl::endStorageLocation() const
{
    if (storeAll())
    {
        return frames_.size();
    }
    if (frames_[0]->frameIndex() == 0 || firstFrameLocation_ == 0)
    {
        return frames_.size() - 1;
    }
    return firstFrameLocation_ - 1;
}


void
AnalysisDataStorage::Impl::extendBuffer(size_t newSize)
{
    frames_.reserve(newSize);
    while (frames_.size() < newSize)
    {
        frames_.push_back(FramePointer(new FrameData(this, nextIndex_)));
        ++nextIndex_;
    }
    // The unused frame should not be included in the count.
    if (!storeAll())
    {
        --nextIndex_;
    }
}


void
AnalysisDataStorage::Impl::rotateBuffer()
{
    GMX_ASSERT(!storeAll(),
               "No need to rotate internal buffer if everything is stored");
    size_t prevFirst = firstFrameLocation_;
    size_t nextFirst = prevFirst + 1;
    if (nextFirst == frames_.size())
    {
        nextFirst = 0;
    }
    firstFrameLocation_ = nextFirst;
    frames_[prevFirst]->clearFrame(nextIndex_ + 1);
    ++nextIndex_;
}


internal::AnalysisDataFrameBuilderPointer
AnalysisDataStorage::Impl::getFrameBuilder()
{
    if (builders_.empty())
    {
        return FrameBuilderPointer(new AnalysisDataStorageFrame(*data_));
    }
    FrameBuilderPointer builder(move(builders_.back()));
    builders_.pop_back();
    return move(builder);
}


void
AnalysisDataStorage::Impl::notifyPointSet(const AnalysisDataPointSetRef &points)
{
    data_->notifyPointsAdd(points);
}


void
AnalysisDataStorage::Impl::notifyNextFrames(size_t firstLocation)
{
    if (firstLocation != firstFrameLocation_)
    {
        // firstLocation can only be zero here if !storeAll() because
        // firstFrameLocation_ is always zero for storeAll()
        int prevIndex =
            (firstLocation == 0 ? frames_.size() - 1 : firstLocation - 1);
        if (!frames_[prevIndex]->isNotified())
        {
            return;
        }
    }
    size_t i   = firstLocation;
    size_t end = endStorageLocation();
    while (i != end)
    {
        Impl::FrameData &storedFrame = *frames_[i];
        if (!storedFrame.isFinished())
        {
            break;
        }
        if (!storedFrame.isNotified())
        {
            data_->notifyFrameStart(storedFrame.header());
            for (int j = 0; j < storedFrame.pointSetCount(); ++j)
            {
                data_->notifyPointsAdd(storedFrame.pointSet(j));
            }
            data_->notifyFrameFinish(storedFrame.header());
            storedFrame.markNotified();
            if (storedFrame.frameIndex() >= storageLimit_)
            {
                rotateBuffer();
            }
        }
        ++i;
        if (!storeAll() && i >= frames_.size())
        {
            i = 0;
        }
    }
}


void
AnalysisDataStorage::Impl::finishFrame(int index)
{
    int                storageIndex = computeStorageLocation(index);
    GMX_RELEASE_ASSERT(storageIndex >= 0, "Out of bounds frame index");
    Impl::FrameData   &storedFrame = *frames_[storageIndex];
    GMX_RELEASE_ASSERT(storedFrame.isStarted(),
                       "finishFrame() called for frame before startFrame()");
    GMX_RELEASE_ASSERT(!storedFrame.isFinished(),
                       "finishFrame() called twice for the same frame");
    GMX_RELEASE_ASSERT(storedFrame.frameIndex() == index,
                       "Inconsistent internal frame indexing");
    builders_.push_back(storedFrame.finishFrame(isMultipoint()));
    if (shouldNotifyImmediately())
    {
        data_->notifyFrameFinish(storedFrame.header());
        if (storedFrame.frameIndex() >= storageLimit_)
        {
            rotateBuffer();
        }
    }
    else
    {
        notifyNextFrames(storageIndex);
    }
}


/********************************************************************
 * AnalysisDataStorageFrame implementation
 */

namespace internal
{

AnalysisDataStorageFrameData::AnalysisDataStorageFrameData(
        AnalysisDataStorage::Impl *storageImpl,
        int                        index)
    : storageImpl_(*storageImpl), header_(index, 0.0, 0.0), status_(eMissing)
{
    GMX_RELEASE_ASSERT(storageImpl->data_ != NULL,
                       "Storage frame constructed before data started");
    // With non-multipoint data, the point set structure is static,
    // so initialize it only once here.
    if (!baseData().isMultipoint())
    {
        int offset = 0;
        for (int i = 0; i < baseData().dataSetCount(); ++i)
        {
            int columnCount = baseData().columnCount(i);
            pointSets_.push_back(
                    AnalysisDataPointSetInfo(offset, columnCount, i, 0));
            offset += columnCount;
        }
    }
}


void
AnalysisDataStorageFrameData::clearFrame(int newIndex)
{
    GMX_RELEASE_ASSERT(!builder_, "Should not clear an in-progress frame");
    status_ = eMissing;
    header_ = AnalysisDataFrameHeader(newIndex, 0.0, 0.0);
    values_.clear();
    if (baseData().isMultipoint())
    {
        pointSets_.clear();
    }
}


void
AnalysisDataStorageFrameData::startFrame(
        const AnalysisDataFrameHeader   &header,
        AnalysisDataFrameBuilderPointer  builder)
{
    status_         = eStarted;
    header_         = header;
    builder_        = move(builder);
    builder_->data_ = this;
    builder_->selectDataSet(0);
}


void
AnalysisDataStorageFrameData::addPointSet(int dataSetIndex, int firstColumn,
                                          ValueIterator begin, ValueIterator end)
{
    const int valueCount  = end - begin;
    if (storageImpl().shouldNotifyImmediately())
    {
        AnalysisDataPointSetInfo pointSetInfo(0, valueCount,
                                              dataSetIndex, firstColumn);
        storageImpl().notifyPointSet(
                AnalysisDataPointSetRef(header(), pointSetInfo,
                                        AnalysisDataValuesRef(begin, end)));
    }
    else
    {
        pointSets_.push_back(
                AnalysisDataPointSetInfo(values_.size(), valueCount,
                                         dataSetIndex, firstColumn));
        std::copy(begin, end, std::back_inserter(values_));
    }
}


AnalysisDataFrameBuilderPointer
AnalysisDataStorageFrameData::finishFrame(bool bMultipoint)
{
    status_ = eFinished;
    if (!bMultipoint)
    {
        GMX_RELEASE_ASSERT(static_cast<int>(pointSets_.size()) == baseData().dataSetCount(),
                           "Point sets created for non-multipoint data");
        values_ = builder_->values_;
        builder_->clearValues();
    }
    else
    {
        GMX_RELEASE_ASSERT(!builder_->bPointSetInProgress_,
                           "Unfinished point set");
    }
    AnalysisDataFrameBuilderPointer builder(move(builder_));
    builder_.reset();
    return move(builder);
}


AnalysisDataPointSetRef
AnalysisDataStorageFrameData::pointSet(int index) const
{
    GMX_ASSERT(index >= 0 && index < pointSetCount(),
               "Invalid point set index");
    return AnalysisDataPointSetRef(
            header_, pointSets_[index],
            AnalysisDataValuesRef(values_.begin(), values_.end()));
}

}   // namespace internal

/********************************************************************
 * AnalysisDataStorageFrame
 */

AnalysisDataStorageFrame::AnalysisDataStorageFrame(
        const AbstractAnalysisData &data)
    : data_(NULL), currentDataSet_(0), currentOffset_(0),
      columnCount_(data.columnCount(0)), bPointSetInProgress_(false)
{
    int totalColumnCount = 0;
    for (int i = 0; i < data.dataSetCount(); ++i)
    {
        totalColumnCount += data.columnCount(i);
    }
    values_.resize(totalColumnCount);
}


AnalysisDataStorageFrame::~AnalysisDataStorageFrame()
{
}


void
AnalysisDataStorageFrame::clearValues()
{
    if (bPointSetInProgress_)
    {
        std::vector<AnalysisDataValue>::iterator i;
        for (i = values_.begin(); i != values_.end(); ++i)
        {
            i->clear();
        }
    }
    bPointSetInProgress_ = false;
}


void
AnalysisDataStorageFrame::selectDataSet(int index)
{
    GMX_RELEASE_ASSERT(data_ != NULL, "Invalid frame accessed");
    const AbstractAnalysisData &baseData = data_->baseData();
    GMX_RELEASE_ASSERT(index >= 0 && index < baseData.dataSetCount(),
                       "Out of range data set index");
    GMX_RELEASE_ASSERT(!baseData.isMultipoint() || !bPointSetInProgress_,
                       "Point sets in multipoint data cannot span data sets");
    currentDataSet_ = index;
    currentOffset_  = 0;
    // TODO: Consider precalculating.
    for (int i = 0; i < index; ++i)
    {
        currentOffset_ += baseData.columnCount(i);
    }
    columnCount_    = baseData.columnCount(index);
}


void
AnalysisDataStorageFrame::finishPointSet()
{
    GMX_RELEASE_ASSERT(data_ != NULL, "Invalid frame accessed");
    GMX_RELEASE_ASSERT(data_->baseData().isMultipoint(),
                       "Should not be called for non-multipoint data");
    if (bPointSetInProgress_)
    {
        std::vector<AnalysisDataValue>::const_iterator begin
            = values_.begin() + currentOffset_;
        std::vector<AnalysisDataValue>::const_iterator end
            = begin + columnCount_;
        int firstColumn = 0;
        while (begin != end && !begin->isSet())
        {
            ++begin;
            ++firstColumn;
        }
        while (end != begin && !(end-1)->isSet())
        {
            --end;
        }
        if (begin == end)
        {
            firstColumn = 0;
        }
        data_->addPointSet(currentDataSet_, firstColumn, begin, end);
    }
    clearValues();
}


void
AnalysisDataStorageFrame::finishFrame()
{
    GMX_RELEASE_ASSERT(data_ != NULL, "Invalid frame accessed");
    data_->storageImpl().finishFrame(data_->frameIndex());
}


/********************************************************************
 * AnalysisDataStorage
 */

AnalysisDataStorage::AnalysisDataStorage()
    : impl_(new Impl())
{
}


AnalysisDataStorage::~AnalysisDataStorage()
{
}


void
AnalysisDataStorage::setParallelOptions(const AnalysisDataParallelOptions &opt)
{
    impl_->pendingLimit_ = 2 * opt.parallelizationFactor() - 1;
}


AnalysisDataFrameRef
AnalysisDataStorage::tryGetDataFrame(int index) const
{
    int storageIndex = impl_->computeStorageLocation(index);
    if (storageIndex == -1)
    {
        return AnalysisDataFrameRef();
    }
    const Impl::FrameData &storedFrame = *impl_->frames_[storageIndex];
    if (!storedFrame.isAvailable())
    {
        return AnalysisDataFrameRef();
    }
    return storedFrame.frameReference();
}


bool
AnalysisDataStorage::requestStorage(int nframes)
{
    // Handle the case when everything needs to be stored.
    if (nframes == -1)
    {
        impl_->storageLimit_ = std::numeric_limits<int>::max();
        return true;
    }
    // Check whether an earlier call has requested more storage.
    if (nframes < impl_->storageLimit_)
    {
        return true;
    }
    impl_->storageLimit_ = nframes;
    return true;
}


void
AnalysisDataStorage::startDataStorage(AbstractAnalysisData *data)
{
    // Data needs to be set before calling extendBuffer()
    impl_->data_ = data;
    if (!impl_->storeAll())
    {
        impl_->extendBuffer(impl_->storageLimit_ + impl_->pendingLimit_ + 1);
    }
}


AnalysisDataStorageFrame &
AnalysisDataStorage::startFrame(const AnalysisDataFrameHeader &header)
{
    GMX_ASSERT(header.isValid(), "Invalid header");
    Impl::FrameData *storedFrame;
    if (impl_->storeAll())
    {
        size_t size = header.index() + 1;
        if (impl_->frames_.size() < size)
        {
            impl_->extendBuffer(size);
        }
        storedFrame = impl_->frames_[header.index()].get();
    }
    else
    {
        int storageIndex = impl_->computeStorageLocation(header.index());
        if (storageIndex == -1)
        {
            GMX_THROW(APIError("Out of bounds frame index"));
        }
        storedFrame = impl_->frames_[storageIndex].get();
    }
    GMX_RELEASE_ASSERT(!storedFrame->isStarted(),
                       "startFrame() called twice for the same frame");
    GMX_RELEASE_ASSERT(storedFrame->frameIndex() == header.index(),
                       "Inconsistent internal frame indexing");
    storedFrame->startFrame(header, impl_->getFrameBuilder());
    if (impl_->shouldNotifyImmediately())
    {
        impl_->data_->notifyFrameStart(header);
    }
    return storedFrame->builder();
}


AnalysisDataStorageFrame &
AnalysisDataStorage::startFrame(int index, real x, real dx)
{
    return startFrame(AnalysisDataFrameHeader(index, x, dx));
}


AnalysisDataStorageFrame &
AnalysisDataStorage::currentFrame(int index)
{
    int                storageIndex = impl_->computeStorageLocation(index);
    GMX_RELEASE_ASSERT(storageIndex >= 0, "Out of bounds frame index");
    Impl::FrameData   &storedFrame = *impl_->frames_[storageIndex];
    GMX_RELEASE_ASSERT(storedFrame.isStarted(),
                       "currentFrame() called for frame before startFrame()");
    GMX_RELEASE_ASSERT(!storedFrame.isFinished(),
                       "currentFrame() called for frame after finishFrame()");
    GMX_RELEASE_ASSERT(storedFrame.frameIndex() == index,
                       "Inconsistent internal frame indexing");
    return storedFrame.builder();
}


void
AnalysisDataStorage::finishFrame(int index)
{
    impl_->finishFrame(index);
}

} // namespace gmx
