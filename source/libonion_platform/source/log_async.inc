/*
 * Optional background writer (onion_log_set_async). The three sinks together
 * cost a caller several milliseconds per record on a console, which is a
 * third of a frame for a process that logs from its render thread. In async
 * mode a record is copied into a small ring and written by a worker thread;
 * the caller only pays for formatting and a short lock.
 *
 * A full ring drops the new record and counts it rather than block the
 * caller. onion_log_emergency() never uses the ring.
 */
#define LOG_ASYNC_SLOTS 128
#define LOG_ASYNC_RECORD_MAX 512

static pthread_mutex_t s_ring_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_ring_wake = PTHREAD_COND_INITIALIZER;
static char s_ring_records[LOG_ASYNC_SLOTS][LOG_ASYNC_RECORD_MAX];
static unsigned short s_ring_lengths[LOG_ASYNC_SLOTS];
static unsigned s_ring_head = 0; /* next slot to write */
static unsigned s_ring_count = 0;
static unsigned s_ring_dropped = 0;
static volatile int s_ring_enabled = 0;

static void write_sinks(const char *msg, size_t len);

static void *async_worker(void *unused) {
  (void)unused;
  char record[LOG_ASYNC_RECORD_MAX];
  for (;;) {
    pthread_mutex_lock(&s_ring_lock);
    while (s_ring_count == 0)
      pthread_cond_wait(&s_ring_wake, &s_ring_lock);
    const unsigned tail =
        (s_ring_head + LOG_ASYNC_SLOTS - s_ring_count) % LOG_ASYNC_SLOTS;
    const size_t len = s_ring_lengths[tail];
    memcpy(record, s_ring_records[tail], len + 1);
    const unsigned dropped = s_ring_dropped;
    s_ring_dropped = 0;
    pthread_mutex_unlock(&s_ring_lock);

    if (dropped != 0) {
      char note[96];
      const int n = snprintf(note, sizeof(note),
                             "[log] %u record(s) dropped: writer fell behind\n",
                             dropped);
      if (n > 0)
        write_sinks(note, (size_t)n);
    }
    write_sinks(record, len);

    /* Release the slot only after the write, so onion_log_flush() can wait
       for "count == 0" and know everything reached the sinks. */
    pthread_mutex_lock(&s_ring_lock);
    s_ring_count--;
    if (s_ring_count == 0)
      pthread_cond_broadcast(&s_ring_wake);
    pthread_mutex_unlock(&s_ring_lock);
  }
}

bool onion_log_set_async(bool enabled) {
  static bool worker_started = false;
  pthread_mutex_lock(&s_ring_lock);
  if (enabled && !worker_started) {
    pthread_t thread;
    if (pthread_create(&thread, NULL, async_worker, NULL) == 0) {
      pthread_detach(thread);
      worker_started = true;
    }
  }
  s_ring_enabled = enabled && worker_started;
  const bool active = s_ring_enabled != 0;
  pthread_mutex_unlock(&s_ring_lock);
  if (!enabled)
    onion_log_flush();
  return active == enabled;
}

void onion_log_flush(void) {
  pthread_mutex_lock(&s_ring_lock);
  while (s_ring_count != 0)
    pthread_cond_wait(&s_ring_wake, &s_ring_lock);
  pthread_mutex_unlock(&s_ring_lock);
}

static bool async_enqueue(const char *msg, size_t len) {
  if (len >= LOG_ASYNC_RECORD_MAX) {
    len = LOG_ASYNC_RECORD_MAX - 1; /* keep the head of an oversized record */
  }
  pthread_mutex_lock(&s_ring_lock);
  if (!s_ring_enabled) {
    pthread_mutex_unlock(&s_ring_lock);
    return false;
  }
  if (s_ring_count == LOG_ASYNC_SLOTS) {
    s_ring_dropped++;
    pthread_mutex_unlock(&s_ring_lock);
    return true;
  }
  char *slot = s_ring_records[s_ring_head];
  memcpy(slot, msg, len);
  if (len > 0 && slot[len - 1] != '\n') {
    slot[len - 1] = '\n'; /* truncated: keep records line-delimited */
  }
  slot[len] = '\0';
  s_ring_lengths[s_ring_head] = (unsigned short)len;
  s_ring_head = (s_ring_head + 1) % LOG_ASYNC_SLOTS;
  s_ring_count++;
  pthread_cond_signal(&s_ring_wake);
  pthread_mutex_unlock(&s_ring_lock);
  return true;
}
