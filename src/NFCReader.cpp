#include "NFCReader.h"

#include <chrono>
#include <utility>

#include "RageLog.h"

#ifdef WITH_NFC
#include <nfc/nfc.h>
#endif

NFCReader::NFCReader() {
#ifdef WITH_NFC
  m_worker = std::thread(&NFCReader::Run, this);
#endif
}

NFCReader::~NFCReader() {
  m_stop = true;

  if (m_worker.joinable()) {
    m_worker.join();
  }
}

std::string NFCReader::TakeScan() {
  std::scoped_lock lock(m_mutex);

  return std::exchange(m_scan, {});
}

void NFCReader::Run() {
#ifdef WITH_NFC
  nfc_context* context = nullptr;
  nfc_init(&context);

  if (!context) {
    LOG->Warn("NFC: libnfc initialization failed");
    return;
  }

  bool reportedMissingReader = false;

  while (!m_stop) {
    nfc_device* device = nfc_open(context, nullptr);

    if (!device) {
      if (!reportedMissingReader) {
        LOG->Warn(
            "NFC: could not open a reader; check libnfc configuration and "
            "device permissions (retrying)");

        reportedMissingReader = true;
      }

      for (int i = 0; i < 30 && !m_stop; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }

      continue;
    }

    reportedMissingReader = false;
    LOG->Info(
        "NFC: opened reader '%s' (%s)", nfc_device_get_name(device),
        nfc_device_get_connstring(device));

    if (nfc_device_set_property_int(device, NP_TIMEOUT_COMMAND, 500) >= 0 &&
        nfc_initiator_init(device) >= 0 &&
        nfc_device_set_property_bool(device, NP_INFINITE_SELECT, false) >= 0) {
      const nfc_modulation modulation = {NMT_ISO14443A, NBR_106};

      while (!m_stop) {
        nfc_target target;
        int result = nfc_initiator_select_passive_target(
            device, modulation, nullptr, 0, &target);

        if (result < 0 && result != NFC_ETIMEOUT) {
          LOG->Warn(
              "NFC: card scan failed: %s (%d); reconnecting",
              nfc_strerror(device), result);

          break;
        }

        if (result > 0) {
          std::string uid;
          const char* hex = "0123456789ABCDEF";

          for (size_t i = 0; i < target.nti.nai.szUidLen; ++i) {
            unsigned char byte = target.nti.nai.abtUid[i];
            uid += hex[byte >> 4];
            uid += hex[byte & 15];
          }

          LOG->Info("NFC: detected card UID %s", uid.c_str());

          {
            std::scoped_lock lock(m_mutex);
            m_scan = uid;
          }

          // Suppress repeats until this target leaves the RF field.
          while (!m_stop &&
                 nfc_initiator_target_is_present(device, &target) == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
          }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    } else {
      LOG->Warn("NFC: reader setup failed: %s; retrying", nfc_strerror(device));
    }

    nfc_close(device);

    for (int i = 0; i < 10 && !m_stop; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  nfc_exit(context);
#endif
}
