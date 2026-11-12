#ifndef NFC_READER_H
#define NFC_READER_H

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

// The worker owns libnfc; only TakeScan crosses into the main thread.
class NFCReader {
 public:
  NFCReader();
  ~NFCReader();
  std::string TakeScan();

 private:
  void Run();

  std::atomic<bool> m_stop{false};
  std::thread m_worker;
  std::mutex m_mutex;
  std::string m_scan;
};

#endif
