/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/encoded_frame.h"

#include <string.h>

#include "absl/types/variant.h"
#include "api/video/video_timing.h"
#include "modules/video_coding/codecs/interface/common_constants.h"
#include "modules/video_coding/codecs/vp8/include/vp8_globals.h"
#include "modules/video_coding/codecs/vp9/include/vp9_globals.h"

namespace webrtc {

VCMEncodedFrame::VCMEncodedFrame()
    : webrtc::EncodedImage(),
      _renderTimeMs(-1),
      _payloadType(0),
      _missingFrame(false),
      _codec(kVideoCodecGeneric) {
  _codecSpecificInfo.codecType = kVideoCodecGeneric;
}

VCMEncodedFrame::VCMEncodedFrame(const VCMEncodedFrame&) = default;

VCMEncodedFrame::~VCMEncodedFrame() {
  Reset();
}

void VCMEncodedFrame::Reset() {
  SetTimestamp(0);
  SetSpatialIndex(absl::nullopt);
  _renderTimeMs = -1;
  _payloadType = 0;
  _frameType = VideoFrameType::kVideoFrameDelta;
  _encodedWidth = 0;
  _encodedHeight = 0;
  _completeFrame = false;
  _missingFrame = false;
  set_size(0);
  _codecSpecificInfo.codecType = kVideoCodecGeneric;
  _codec = kVideoCodecGeneric;
  rotation_ = kVideoRotation_0;
  content_type_ = VideoContentType::UNSPECIFIED;
  timing_.flags = VideoSendTiming::kInvalid;
}

void VCMEncodedFrame::CopyCodecSpecific(const RTPVideoHeader* header) {
  if (header) {
    switch (header->codec) {
      case kVideoCodecVP8: {
        const auto& vp8_header =
            absl::get<RTPVideoHeaderVP8>(header->video_type_header);
        if (_codecSpecificInfo.codecType != kVideoCodecVP8) {
          // This is the first packet for this frame.
          _codecSpecificInfo.codecSpecific.VP8.temporalIdx = 0;
          _codecSpecificInfo.codecSpecific.VP8.layerSync = false;
          _codecSpecificInfo.codecSpecific.VP8.keyIdx = -1;
          _codecSpecificInfo.codecType = kVideoCodecVP8;
        }
        _codecSpecificInfo.codecSpecific.VP8.nonReference =
            vp8_header.nonReference;
        if (vp8_header.temporalIdx != kNoTemporalIdx) {
          _codecSpecificInfo.codecSpecific.VP8.temporalIdx =
              vp8_header.temporalIdx;
          _codecSpecificInfo.codecSpecific.VP8.layerSync = vp8_header.layerSync;
        }
        if (vp8_header.keyIdx != kNoKeyIdx) {
          _codecSpecificInfo.codecSpecific.VP8.keyIdx = vp8_header.keyIdx;
        }
        break;
      }
      case kVideoCodecVP9: {
        const auto& vp9_header =
            absl::get<RTPVideoHeaderVP9>(header->video_type_header);
        if (_codecSpecificInfo.codecType != kVideoCodecVP9) {
          // This is the first packet for this frame.
          _codecSpecificInfo.codecSpecific.VP9.temporal_idx = 0;
          _codecSpecificInfo.codecSpecific.VP9.gof_idx = 0;
          _codecSpecificInfo.codecSpecific.VP9.inter_layer_predicted = false;
          _codecSpecificInfo.codecType = kVideoCodecVP9;
        }
        _codecSpecificInfo.codecSpecific.VP9.inter_pic_predicted =
            vp9_header.inter_pic_predicted;
        _codecSpecificInfo.codecSpecific.VP9.flexible_mode =
            vp9_header.flexible_mode;
        _codecSpecificInfo.codecSpecific.VP9.num_ref_pics =
            vp9_header.num_ref_pics;
        for (uint8_t r = 0; r < vp9_header.num_ref_pics; ++r) {
          _codecSpecificInfo.codecSpecific.VP9.p_diff[r] =
              vp9_header.pid_diff[r];
        }
        _codecSpecificInfo.codecSpecific.VP9.ss_data_available =
            vp9_header.ss_data_available;
        if (vp9_header.temporal_idx != kNoTemporalIdx) {
          _codecSpecificInfo.codecSpecific.VP9.temporal_idx =
              vp9_header.temporal_idx;
          _codecSpecificInfo.codecSpecific.VP9.temporal_up_switch =
              vp9_header.temporal_up_switch;
        }
        if (vp9_header.spatial_idx != kNoSpatialIdx) {
          _codecSpecificInfo.codecSpecific.VP9.inter_layer_predicted =
              vp9_header.inter_layer_predicted;
          SetSpatialIndex(vp9_header.spatial_idx);
        }
        if (vp9_header.gof_idx != kNoGofIdx) {
          _codecSpecificInfo.codecSpecific.VP9.gof_idx = vp9_header.gof_idx;
        }
        if (vp9_header.ss_data_available) {
          _codecSpecificInfo.codecSpecific.VP9.num_spatial_layers =
              vp9_header.num_spatial_layers;
          _codecSpecificInfo.codecSpecific.VP9
              .spatial_layer_resolution_present =
              vp9_header.spatial_layer_resolution_present;
          if (vp9_header.spatial_layer_resolution_present) {
            for (size_t i = 0; i < vp9_header.num_spatial_layers; ++i) {
              _codecSpecificInfo.codecSpecific.VP9.width[i] =
                  vp9_header.width[i];
              _codecSpecificInfo.codecSpecific.VP9.height[i] =
                  vp9_header.height[i];
            }
          }
          _codecSpecificInfo.codecSpecific.VP9.gof.CopyGofInfoVP9(
              vp9_header.gof);
        }
        break;
      }
      case kVideoCodecH264: {
        _codecSpecificInfo.codecType = kVideoCodecH264;

        // The following H264 codec specific data are not used elsewhere.
        // Instead they are read directly from the frame marking extension.
        // These codec specific data structures should be removed
        // when frame marking is used.
        _codecSpecificInfo.codecSpecific.H264.temporal_idx = kNoTemporalIdx;
        if (header->frame_marking.temporal_id != kNoTemporalIdx) {
          _codecSpecificInfo.codecSpecific.H264.temporal_idx =
              header->frame_marking.temporal_id;
          _codecSpecificInfo.codecSpecific.H264.base_layer_sync =
              header->frame_marking.base_layer_sync;
          _codecSpecificInfo.codecSpecific.H264.idr_frame =
              header->frame_marking.independent_frame;
        }
        break;
      }
      default: {
        _codecSpecificInfo.codecType = kVideoCodecGeneric;
        break;
      }
    }
  }
}

void VCMEncodedFrame::VerifyAndAllocate(size_t minimumSize) {
  size_t old_capacity = capacity();
  if (minimumSize > old_capacity) {
    // TODO(nisse): EncodedImage::Allocate is implemented as a realloc
    // operation, and is deprecated. Refactor to use EncodedImageBuffer::Realloc
    // instead.
    Allocate(minimumSize);
  }
}

}  // namespace webrtc
