#include "VirtualMemory.h"
#include "PhysicalMemory.h"

// number of bits used in the address variable (currently uint64_t)
#define TOTAL_BITS_NUM 64

/**
 * @brief Initializes a frame's memory to 0.
 * @param frameToClear The frame to clear.
 */
void clearFrame(uint64_t frameToClear)
{
  for (uint64_t i = 0; i < PAGE_SIZE; ++i)
    {
      PMwrite(frameToClear * PAGE_SIZE + i, 0);
    }
}

/**
 * get the OFFSET_WIDTH least significant bits.
 * @param address the address to extract offset from
 * @return uint64_t the offset bits
 */
uint64_t getOffset(uint64_t address)
{
  uint64_t shiftDistance = TOTAL_BITS_NUM - OFFSET_WIDTH;
  return (address << shiftDistance) >> shiftDistance;
}

/**
 * get the cyclic distance between two pages.
 * @param page1
 * @param page2
 * @return uint64_t the cyclic distance between the two pages
 */
uint64_t getCyclicDist(uint64_t page1, uint64_t page2)
{
  // return min{NUM_PAGES - |page1 - page2|, |page1 - page2|}
  uint64_t dist = page1 > page2 ? page1 - page2 : page2 - page1;
  return dist < NUM_PAGES - dist ? dist : NUM_PAGES - dist;
}

/**
 * recursive function that traverses the frame table and find out what the max
 * used frame is, is there an empty frame, and finds a candidate frame to evict
 * if needed.
 * @param currFrame The frame we are currently on
 * @param currParentAdd The address that points to the currFrame
 * @param currDepth The level on the tree currFrame is on
 * @param maxFrame a pointer to the max frame in use on the tree (0-NUM_OF_FRAMES)
 *        default is 0
 * @param emptyFrame a pointer to an empty frame if one is found. default is 0.
 * @param emptyParentAdd The address that points to the emptyFrame
 * @param maxDistFrame a pointer to the frame with the page with max distance
 *        to the virtualAddress we are looking for.
 * @param maxDistParentAdd The address that points to the maxDistFrame
 * @param maxDistPage The page with the max dist.
 * @param virtualAdd the VirtualAdd user is trying to write/read to/from
 * @param lastCreatedFrame the frame last created in the process. default is 0.
 * @param currPageAdd the page of the current frame (updated during traversal
 *        reaching correct value when reaching a leaf.)
 */
void findNewFrameHelper(word_t currFrame, uint64_t currParentAdd, uint64_t currDepth,
                        word_t* maxFrame, word_t* emptyFrame, uint64_t* emptyParentAdd,
                        word_t* maxDistFrame, uint64_t* maxDistParentAdd, uint64_t* maxDistPage,
                        uint64_t virtualAdd, word_t lastCreatedFrame, uint64_t currPageAdd)
{
  // if an empty frame was found, no need to keep traversing the table
  if (*emptyFrame != 0)
      return;
  bool isEmpty = true;
  word_t value;
  // go over each frame currFrame is pointing to. Check for maxFrame, and if
  // it's a leaf check maxDistFrame. Else, recurse into it.
  for (uint64_t i = 0; i < PAGE_SIZE; i++)
  {
      PMread (currFrame * PAGE_SIZE + i, &value);
      if (value != 0)
      {
        if (value > *maxFrame)
          *maxFrame = value;
        isEmpty = false;
        uint64_t ithPageAdd = (currPageAdd << OFFSET_WIDTH) + i;
        if (currDepth == TABLES_DEPTH - 1)
        {
          uint64_t currDistance = getCyclicDist ((virtualAdd >> OFFSET_WIDTH), ithPageAdd);
          uint64_t maxDistance = getCyclicDist ((virtualAdd >> OFFSET_WIDTH), *maxDistPage);
          if (currDistance > maxDistance || *maxDistFrame == 0)
          {
            *maxDistParentAdd = currFrame * PAGE_SIZE + i;
            *maxDistFrame = value;
            *maxDistPage = ithPageAdd;
          }
          continue;
        }
        else
        {
            findNewFrameHelper (value,
                                currFrame * PAGE_SIZE + i,
                                currDepth + 1, maxFrame,
                                emptyFrame, emptyParentAdd, maxDistFrame, maxDistParentAdd,
                                maxDistPage, virtualAdd, lastCreatedFrame, ithPageAdd);
        }
      }
  }
  // check the empty frame isn't the frame last created.
  if (isEmpty & (currFrame != lastCreatedFrame))
  {
    *emptyFrame = currFrame;
    *emptyParentAdd = currParentAdd;
    return;
  }
}


void findNewFrame (word_t *frame, uint64_t virtualAdd, word_t lastCreatedFrame)
{
  word_t maxFrame = 0;
  word_t emptyFrame = 0;
  word_t maxDistFrame = 0;
  uint64_t emptyParentAdd = 0;
  uint64_t maxDistParentAdd = 0;
  uint64_t maxDistPage = 0;
  findNewFrameHelper (0, 0, 0, &maxFrame, &emptyFrame,
                      &emptyParentAdd, &maxDistFrame, &maxDistParentAdd,
                      &maxDistPage, virtualAdd, lastCreatedFrame, 0);
  // order of importance: Empty -> Max -> Evict
  if (emptyFrame != 0)
  {
    PMwrite (emptyParentAdd, 0);
    *frame = emptyFrame;
  }
  else if (maxFrame + 1 < NUM_FRAMES)
  {
      *frame = maxFrame + 1;
  }
  else
  {
    PMwrite (maxDistParentAdd, 0);
    PMevict (maxDistFrame, maxDistPage);
    clearFrame (maxDistFrame);
    *frame = maxDistFrame;
    }
}

/**
 *  translates a virtual address to a pyshical one and stores the relevant
 *  frame to frameToSave.
 * @param virtualAdd address to translate
 * @param frameToSave a pointer to the relevant frame to store result in
 */
void translateVirtualAdd(uint64_t virtualAdd, word_t* frameToSave)
{
  word_t add1 = 0;
  word_t currFrame;
  uint64_t currValue;
  uint64_t depth = TABLES_DEPTH;
  for (uint64_t level = depth; level > 0; level--)
    {
      currValue = getOffset(virtualAdd >> (level * OFFSET_WIDTH));
      currFrame = add1;
      PMread(currFrame * PAGE_SIZE + currValue, &add1);
      if (add1 == 0)
      {
        // if frame not found, find one to use
        findNewFrame (&add1, virtualAdd, currFrame);
        PMwrite (currFrame * PAGE_SIZE + currValue, add1);
        }
    }
  PMrestore (add1, virtualAdd >> OFFSET_WIDTH);
  // store result in frameToSave
  PMread (currFrame * PAGE_SIZE + getOffset (virtualAdd >> OFFSET_WIDTH), frameToSave);
}

/**
 * Initialize the table (clear frame 0).
 */
void VMinitialize()
{
  clearFrame (0);
}

/**
 * read data in virtual address and store it in value pointer
 * @param virtualAddress
 * @param value
 * @return 0 on failure, 1 on success
 */
int VMread(uint64_t virtualAddress, word_t* value)
{
  if (virtualAddress > VIRTUAL_MEMORY_SIZE - 1)
    return 0;
  word_t frame;
  translateVirtualAdd (virtualAddress, &frame);
  PMread (frame * PAGE_SIZE + getOffset (virtualAddress), value);
  return 1;
}

int VMwrite(uint64_t virtualAddress, word_t value)
{
  if (virtualAddress > VIRTUAL_MEMORY_SIZE - 1)
    return 0;
  word_t frame;
  translateVirtualAdd (virtualAddress, &frame);
  PMwrite (frame * PAGE_SIZE + getOffset (virtualAddress), value);
  return 1;
}

