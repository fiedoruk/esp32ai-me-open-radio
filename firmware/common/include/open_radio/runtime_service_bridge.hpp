#pragma once

#include <cstdint>

#include "open_radio/runtime_ingress.hpp"
#include "open_radio/service_contracts.hpp"

namespace open_radio {

class RuntimeServiceBridge {
 public:
  explicit RuntimeServiceBridge(RuntimeIngress& ingress) : ingress_(ingress) {}

  bool boot(const StorageSelectionDto& storage, std::uint32_t epoch,
            std::uint32_t producerSequence, std::uint32_t rawTick) {
    if (storage.status == StorageStatus::Bootable) {
      return emit(RuntimeProducer::Storage, RuntimeEventKind::ConfigReady, epoch,
                  producerSequence, rawTick);
    }
    const bool empty = storage.rejectionA == StorageRejectReason::Empty &&
                       storage.rejectionB == StorageRejectReason::Empty;
    return emit(RuntimeProducer::Storage,
                empty ? RuntimeEventKind::ConfigMissing
                      : RuntimeEventKind::ConfigCorrupt,
                epoch, producerSequence, rawTick);
  }

  bool network(const NetworkSelectionDto& selection, std::uint32_t epoch,
               std::uint32_t producerSequence, std::uint32_t rawTick) {
    return emit(RuntimeProducer::Wifi,
                selection.status == NetworkSelectionStatus::Selected
                    ? RuntimeEventKind::WifiConnected
                    : RuntimeEventKind::WifiLost,
                epoch, producerSequence, rawTick);
  }

  bool resolver(const ResolverResultDto& result, std::uint32_t epoch,
                std::uint32_t producerSequence, std::uint32_t rawTick) {
    if (result.status == ResolverStatus::Unsupported) {
      return emit(RuntimeProducer::Resolver,
                  RuntimeEventKind::ResolverUnsupported, epoch,
                  producerSequence, rawTick);
    }
    return emit(RuntimeProducer::Resolver,
                result.status == ResolverStatus::Playing
                    ? RuntimeEventKind::ResolverReady
                    : RuntimeEventKind::StreamStalled,
                epoch, producerSequence, rawTick);
  }

  bool stream(bool healthy, std::uint32_t epoch,
              std::uint32_t producerSequence, std::uint32_t rawTick) {
    return emit(RuntimeProducer::Stream,
                healthy ? RuntimeEventKind::StreamHealthy
                        : RuntimeEventKind::StreamStalled,
                epoch, producerSequence, rawTick);
  }

  bool bluetoothRemembered(std::uint32_t epoch,
                           std::uint32_t producerSequence,
                           std::uint32_t rawTick) {
    return emit(RuntimeProducer::Bluetooth,
                RuntimeEventKind::BluetoothRemembered, epoch,
                producerSequence, rawTick);
  }

  bool bluetoothConnection(bool connected, std::uint32_t epoch,
                           std::uint32_t producerSequence,
                           std::uint32_t rawTick) {
    return emit(RuntimeProducer::Bluetooth,
                connected ? RuntimeEventKind::BluetoothConnected
                          : RuntimeEventKind::BluetoothLost,
                epoch, producerSequence, rawTick);
  }

  bool localOutput(bool available, std::uint32_t epoch,
                   std::uint32_t producerSequence, std::uint32_t rawTick) {
    return emit(RuntimeProducer::LocalOutput,
                available ? RuntimeEventKind::LocalOutputAvailable
                          : RuntimeEventKind::LocalOutputLost,
                epoch, producerSequence, rawTick);
  }

  bool powerInterrupted(std::uint32_t epoch, std::uint32_t producerSequence,
                        std::uint32_t rawTick) {
    return emit(RuntimeProducer::Power, RuntimeEventKind::PowerInterrupted,
                epoch, producerSequence, rawTick);
  }

 private:
  bool emit(RuntimeProducer producer, RuntimeEventKind kind,
            std::uint32_t epoch, std::uint32_t producerSequence,
            std::uint32_t rawTick) {
    return ingress_.post({producer, epoch, producerSequence, rawTick, kind});
  }

  RuntimeIngress& ingress_;
};

}
