## **LibVT Threading overview**

### Controls

Define `ENABLE_MT` as follows to control threading:

* **0** - Single threaded.
* **1** - One main thread, and one background thread for page loading from disk.
* **2** - One main thread, and two background threads: one for page loading, the other for decompression.
 
### Components

* **Pages** are texture tiles from the tiled mipmap pyramid stored on disk.
* **Main thread** determines visible pages from tiled mipmap pyramid, then requests the **background thread** to fetch them from disk (`neededPages` queue).
* **Background thread** fetches pages from disk (decompressing from file format and recompressing to GPU format, as needed) (`newPages` queue).
* **Background thread** also saves these fetched pages to the in-memory page cache.  In fact, in-memory page cache is checked before fetching the page from disk (`cachedPages` map)
* **Main thread** regularly checks the newPages queue, uploading any new pages to GPU and then evicting them from newPages queue.  Main thread also evicts pages from in-memory page cache based on how long since page was last accessed.

### Benefits

* This allows expensive file I/O and decompression to happen in the background thread, without freezing the main rendering loop where the user demands smooth interaction.  This takes advantage of multiple CPU cores to do work in parallel.
* This also allows utilizing system memory as an intermediate fast and spacious cache between disk and GPU, with system memory being far faster than disk and likely more capicious than GPU cache.


## **LibVT Threading details**

Here's a break down of what the synchronization primitives do when more than one thread is enabled.


#### 1. **`neededPagesMutex`**

One-at-a-time access to the `neededPages` queue. Main thread produces requests (load from disk), Background thread consumes requests (load into system memory).

* **Protects**: The `neededPages` queue. This queue holds the list of texture pages that the main thread has determined are needed but are not yet loaded into the physical texture.  
* **Who uses it**:  
  * **Producer (Main Thread)**: When the main thread renders a frame, it identifies missing pages and `push_back`s them into the `neededPages` queue. It must lock this mutex to do so.  
  * **Consumer (Background Thread)**: The `vtLoadNeededPages` function (running in a background thread) locks this mutex to safely check if the queue is empty and to `pop_front` page requests to begin loading them.  
* **Why it's needed**: Without it, the main thread could be adding a page to the queue at the exact same moment the background thread is trying to remove one, corrupting the queue's internal state.

#### 2. **`neededPagesAvailableCondition`**

Messaging for the `neededPages` queue, such that the Background consuming thread can sleep when idle, and be woken up when there's work to do.

* **Coordinates**: The `neededPages` workflow. It's the communication channel between the main thread (producer) and the background thread (consumer).  
* **Who uses it**:  
  * **Waiter (Background Thread)**: In `vtLoadNeededPages`, if the thread wakes up and finds the `neededPages` queue is empty, it calls `wait()` on this condition variable. This puts the thread to sleep, consuming no CPU, until there is work to do.  
  * **Notifier (Main Thread)**: After the main thread adds one or more pages to the `neededPages` queue, it calls `notify_one()` on this condition variable to wake up the sleeping background thread.  
* **Why it's needed**: This is the key to efficiency. Instead of the background thread running a `while(true)` loop that constantly locks the mutex and checks the queue size, it can sleep peacefully, knowing it will be woken up precisely when new work is available.

#### 3. **`newPagesMutex`**

One-at-a-time access to the `newPages` queue. Background thread produces pages (disk to system memory), Main thread consumes pages (system memory to GPU).

* **Protects**: The `newPages` queue. This queue is the "outbox" for the background thread. It holds pages that have been successfully loaded from disk and decompressed, and are now ready to be uploaded to the GPU.  
* **Who uses it**:  
  * **Producer (Background Thread)**: After `vtLoadNeededPages` finishes loading and preparing a page, it locks this mutex to `push` the completed `pageInfo` into the `newPages` queue.  
  * **Consumer (Main Thread)**: During its update cycle, the main thread locks this mutex to check for and retrieve completed pages from the `newPages` queue, which it then uploads to the physical texture on the GPU.  
* **Why it's needed**: It provides a thread-safe handoff mechanism for completed work from the background thread back to the main thread.

#### **`cachedPagesMutex`**

One-at-a-time access to the in-memory page cache, `cachedPages`.  Background thread adds pages to this cache (disk to system memory), Main thread removes pages from this cache (evict from system memory).

* **Protects**: The in-memory page cache, specifically `cachedPages` and `cachedPagesAccessTimes`. This cache stores raw, decompressed page data to avoid re-reading and re-decompressing from disk if a page is needed again soon.  
* **Who uses it**:  
  * **Background Thread**: Before loading a page from disk, it locks this mutex to check if the page is already in the cache (`vtcIsPageInCacheLOCK`). If not, after loading, it locks it again to insert the new page data (`vtcInsertPageIntoCacheLOCK`).  
  * **Main Thread**: The main thread also interacts with the cache, for example, to evict pages that haven't been used recently (`vtcReduceCacheIfNecessaryLOCK`).  
* **Why it's needed**: Since both the main thread and background thread can read from, write to, and delete from the cache, this mutex is critical to prevent the cache's data structures (`std::map`) from being corrupted.

In summary, these primitives work together to create a multi-stage, thread-safe pipeline.
