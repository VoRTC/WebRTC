/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <memory>

#include "api/rtp_packet_infos.h"
#include "modules/video_coding/frame_object.h"
#include "modules/video_coding/packet_buffer.h"
#include "modules/video_coding/rtp_frame_reference_finder.h"
#include "system_wrappers/include/clock.h"

namespace webrtc {

namespace {
struct DataReader {
  DataReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  void CopyTo(void* destination, size_t size) {
    uint8_t* dest = reinterpret_cast<uint8_t*>(destination);
    size_t num_bytes = std::min(size_ - offset_, size);
    memcpy(dest, data_ + offset_, num_bytes);
    offset_ += num_bytes;

    size -= num_bytes;
    if (size > 0)
      memset(dest + num_bytes, 0, size);
  }

  template <typename T>
  T GetNum() {
    T res;
    if (offset_ + sizeof(res) < size_) {
      memcpy(&res, data_ + offset_, sizeof(res));
      offset_ += sizeof(res);
      return res;
    }

    offset_ = size_;
    return T(0);
  }

  bool MoreToRead() { return offset_ < size_; }

  const uint8_t* data_;
  size_t size_;
  size_t offset_ = 0;
};

class NullCallback : public video_coding::OnCompleteFrameCallback {
  void OnCompleteFrame(
      std::unique_ptr<video_coding::EncodedFrame> frame) override {}
};

class FuzzyPacketBuffer : public video_coding::PacketBuffer {
 public:
  explicit FuzzyPacketBuffer(DataReader* reader)
      : PacketBuffer(nullptr, 2, 4, nullptr), reader(reader) {
    switch (reader->GetNum<uint8_t>() % 3) {
      case 0:
        codec = kVideoCodecVP8;
        break;
      case 1:
        codec = kVideoCodecVP9;
        break;
      case 2:
        codec = kVideoCodecH264;
        break;
    }
  }

  VCMPacket* GetPacket(uint16_t seq_num) override {
    auto packet_it = packets.find(seq_num);
    if (packet_it != packets.end())
      return &packet_it->second;

    VCMPacket* packet = &packets[seq_num];
    packet->video_header.codec = codec;
    switch (codec) {
      case kVideoCodecVP8:
        packet->video_header.video_type_header.emplace<RTPVideoHeaderVP8>();
        break;
      case kVideoCodecVP9:
        packet->video_header.video_type_header.emplace<RTPVideoHeaderVP9>();
        break;
      case kVideoCodecH264:
        packet->video_header.video_type_header.emplace<RTPVideoHeaderH264>();
        break;
      default:
        RTC_NOTREACHED();
    }
    packet->markerBit = true;
    reader->CopyTo(packet, sizeof(packet));
    return packet;
  }

 private:
  std::map<uint16_t, VCMPacket> packets;
  VideoCodecType codec;
  DataReader* const reader;
};
}  // namespace

void FuzzOneInput(const uint8_t* data, size_t size) {
  if (size > 20000) {
    return;
  }
  DataReader reader(data, size);
  FuzzyPacketBuffer packet_buffer(&reader);
  NullCallback cb;
  video_coding::RtpFrameReferenceFinder reference_finder(&cb);

  while (reader.MoreToRead()) {
    // Make sure that these packets fulfill the contract of RtpFrameObject.
    uint16_t first_seq_num = reader.GetNum<uint16_t>();
    uint16_t last_seq_num = reader.GetNum<uint16_t>();
    VCMPacket* first_packet = packet_buffer.GetPacket(first_seq_num);
    VCMPacket* last_packet = packet_buffer.GetPacket(last_seq_num);
    first_packet->video_header.is_first_packet_in_frame = true;
    last_packet->video_header.is_last_packet_in_frame = true;

    auto frame = std::make_unique<video_coding::RtpFrameObject>(
        &packet_buffer, first_seq_num, last_seq_num, 0, 0, 0, RtpPacketInfos(),
        EncodedImageBuffer::Create(/*size=*/0));
    reference_finder.ManageFrame(std::move(frame));
  }
}

}  // namespace webrtc
