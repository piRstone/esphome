#include "zigbee_attribute_esp32.h"
#include "esphome/core/log.h"
#include "esphome/core/defines.h"
#ifdef USE_ESP32
#ifdef USE_ZIGBEE

namespace esphome::zigbee {

ESPHOME_LOG_TAG(TAG, "zigbee.attribute");

void ZigbeeAttribute::set_attr_() {
  if (!this->zb_->is_started()) {
    return;
  }
  if (esp_zigbee_lock_acquire(10 / portTICK_PERIOD_MS)) {
    ezb_zcl_status_t state = ezb_zcl_set_attr_value(this->endpoint_id_, this->cluster_id_, this->role_, this->attr_id_,
                                                    EZB_ZCL_STD_MANUF_CODE, this->value_p_, false);
    // cleared before report_() so it can disable the loop
    // when the report has to wait for join
    this->set_attr_requested_ = false;
    if (this->force_report_) {
      this->report_(true);
    }
    // Check for error
    if (state != EZB_ZCL_STATUS_SUCCESS) {
      ESP_LOGE(TAG, "Setting attribute 0x%04X on cluster 0x%04X failed, ZCL status: %u", this->attr_id_,
               this->cluster_id_, static_cast<unsigned>(state));
    }
    esp_zigbee_lock_release();
  }
}

void ZigbeeAttribute::report_confirm_(
    ezb_zcl_cmd_cnf_t *cnf,
    void *user_ctx) {

  auto *self =
      static_cast<ZigbeeAttribute *>(user_ctx);

  if (self == nullptr) {
    return;
  }

  const bool success =
      cnf != nullptr &&
      cnf->status == 0;

  self->zb_->report_tx_done(success);

  if (cnf != nullptr) {

    ESP_LOGD(
      TAG,
      "Report confirmed: ep=%u cluster=0x%04X tsn=%u status=0x%02X",
      cnf->src_ep,
      cnf->cluster_id,
      cnf->tsn,
      cnf->status
    );

  } else {

    ESP_LOGW(
      TAG,
      "Report confirmation received without data"
    );
  }
}

void ZigbeeAttribute::report_(bool has_lock) {
  if (!this->report_enabled) {
    return;
  }
  if (!this->zb_->is_joined()) {
    this->report_requested_ = true;
    if (!this->set_attr_requested_) {
      this->disable_loop();
    }
    return;
  }
  if (has_lock or esp_zigbee_lock_acquire(10 / portTICK_PERIOD_MS)) {
    ezb_zcl_report_attr_cmd_t cmd = {};
    cmd.cmd_ctrl.fc.direction = EZB_ZCL_CMD_DIRECTION_TO_CLI;
    cmd.cmd_ctrl.fc.dis_default_rsp = 1;
    cmd.cmd_ctrl.dst_addr.addr_mode = EZB_ADDR_MODE_SHORT;
    cmd.cmd_ctrl.dst_addr.u.short_addr = 0x0000;
    cmd.cmd_ctrl.dst_ep = 1;
    cmd.cmd_ctrl.src_ep = this->endpoint_id_;
    cmd.cmd_ctrl.cluster_id = this->cluster_id_;
    cmd.cmd_ctrl.fc.manuf_specific = 0;

    // cmd.payload.attr_id = this->attr_id_;
    // ezb_zcl_report_attr_cmd_req(&cmd);
    // this->report_requested_ = false;

    cmd.payload.attr_id = this->attr_id_;

    // ============================================================
    // Callback appelé lorsque CETTE commande Zigbee a terminé
    // sa transmission.
    // ============================================================

    cmd.cmd_ctrl.cnf_ctx.cb =
        &ZigbeeAttribute::report_confirm_;

    cmd.cmd_ctrl.cnf_ctx.user_ctx =
        this;

    // ============================================================
    // Nous allons maintenant attendre une confirmation.
    // ============================================================

    this->zb_->report_tx_started();

    // ============================================================
    // Demande d'émission du report.
    // ============================================================

    ezb_err_t err =
        ezb_zcl_report_attr_cmd_req(&cmd);

    // ============================================================
    // Si la pile refuse immédiatement la commande,
    // aucun callback ultérieur n'est normalement attendu.
    // ============================================================

    if (err != EZB_ERR_NONE) {

      ESP_LOGW(
        TAG,
        "Failed to queue Zigbee report: error=%d",
        static_cast<int>(err)
      );

      this->zb_->report_tx_done(false);
    }

    this->report_requested_ = false;

    if (!has_lock) {
      esp_zigbee_lock_release();
    }
  }
}

void ZigbeeAttribute::set_report(ZigbeeReportT report) {
  if (report == ZigbeeReportT::ZIGBEE_REPORT_DEFAULT || report == ZigbeeReportT::ZIGBEE_REPORT_COORDINATOR) {
    return;
  }
  this->report_enabled = true;
  if (report == ZigbeeReportT::ZIGBEE_REPORT_FORCE) {
    this->force_report_ = true;
  }
  this->zb_->add_on_join_callback([this](bool) {
    if (this->report_requested_) {
      this->enable_loop();
    }
  });
}

void ZigbeeAttribute::report() {
  if (!this->report_enabled) {
    return;
  }
  this->report_requested_ = true;
  this->enable_loop();
}

void ZigbeeAttribute::loop() {
  if (this->set_attr_requested_) {
    this->set_attr_();
  }

  if (this->report_requested_) {
    this->report_(false);
  }

  if (!this->report_requested_ && !this->set_attr_requested_) {
    this->disable_loop();
  }
}

}  // namespace esphome::zigbee

#endif
#endif
