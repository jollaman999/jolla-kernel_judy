/* Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <cam_sensor_cmn_header.h>
#include "cam_actuator_core.h"
#include "cam_sensor_util.h"
#include "cam_trace.h"
#include "cam_res_mgr_api.h"

#ifdef CONFIG_MACH_SDM845_JUDYPN
extern bool msm_act_data_enqueue(uint32_t reg_addr, uint32_t reg_data,
		struct cam_actuator_ctrl_t *a_ctrl);
#endif

int32_t cam_actuator_construct_default_power_setting(
	struct cam_sensor_power_ctrl_t *power_info)
{
	int rc = 0;

	power_info->power_setting_size = 1;
	power_info->power_setting =
		(struct cam_sensor_power_setting *)
		kzalloc(sizeof(struct cam_sensor_power_setting),
			GFP_KERNEL);
	if (!power_info->power_setting)
		return -ENOMEM;

	power_info->power_setting[0].seq_type = SENSOR_VAF;
	power_info->power_setting[0].seq_val = CAM_VAF;
	power_info->power_setting[0].config_val = 1;

    /*LGE_CHANGE_S, Added power up delay for storm vcm, 2018-06-22, dongsu.bag@lge.com */
#ifdef CONFIG_MACH_SDM845_JUDYPN
	power_info->power_setting[0].delay = 20;
#else
    power_info->power_setting[0].delay = 2;
#endif
    /*LGE_CHANGE_E, Added power up delay for storm vcm, 2018-06-22, dongsu.bag@lge.com */

	power_info->power_down_setting_size = 1;
	power_info->power_down_setting =
		(struct cam_sensor_power_setting *)
		kzalloc(sizeof(struct cam_sensor_power_setting),
			GFP_KERNEL);
	if (!power_info->power_down_setting) {
		rc = -ENOMEM;
		goto free_power_settings;
	}

	power_info->power_down_setting[0].seq_type = SENSOR_VAF;
	power_info->power_down_setting[0].seq_val = CAM_VAF;
	power_info->power_down_setting[0].config_val = 0;

	return rc;

free_power_settings:
	kfree(power_info->power_setting);
	return rc;
}

static int32_t cam_actuator_power_up(struct cam_actuator_ctrl_t *a_ctrl)
{
	int rc = 0;
	struct cam_hw_soc_info  *soc_info =
		&a_ctrl->soc_info;
	struct cam_actuator_soc_private  *soc_private;
	struct cam_sensor_power_ctrl_t *power_info;

	soc_private =
		(struct cam_actuator_soc_private *)a_ctrl->soc_info.soc_private;
	power_info = &soc_private->power_info;

	if ((power_info->power_setting == NULL) &&
		(power_info->power_down_setting == NULL)) {
		CAM_INFO(CAM_ACTUATOR,
			"Using default power settings");
		rc = cam_actuator_construct_default_power_setting(power_info);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR,
				"Construct default actuator power setting failed.");
			return rc;
		}
	}

	/* Parse and fill vreg params for power up settings */
	rc = msm_camera_fill_vreg_params(
		&a_ctrl->soc_info,
		power_info->power_setting,
		power_info->power_setting_size);
	if (rc) {
		CAM_ERR(CAM_ACTUATOR,
			"failed to fill vreg params for power up rc:%d", rc);
		return rc;
	}

	/* Parse and fill vreg params for power down settings*/
	rc = msm_camera_fill_vreg_params(
		&a_ctrl->soc_info,
		power_info->power_down_setting,
		power_info->power_down_setting_size);
	if (rc) {
		CAM_ERR(CAM_ACTUATOR,
			"failed to fill vreg params power down rc:%d", rc);
		return rc;
	}

	power_info->dev = soc_info->dev;

	rc = cam_sensor_core_power_up(power_info, soc_info);
	if (rc) {
		CAM_ERR(CAM_ACTUATOR,
			"failed in actuator power up rc %d", rc);
		return rc;
	}

	rc = camera_io_init(&a_ctrl->io_master_info);
	if (rc < 0)
		CAM_ERR(CAM_ACTUATOR, "cci init failed: rc: %d", rc);

	return rc;
}

static int32_t cam_actuator_power_down(struct cam_actuator_ctrl_t *a_ctrl)
{
	int32_t rc = 0;
	struct cam_sensor_power_ctrl_t *power_info;
	struct cam_hw_soc_info *soc_info = &a_ctrl->soc_info;
	struct cam_actuator_soc_private  *soc_private;

	if (!a_ctrl) {
		CAM_ERR(CAM_ACTUATOR, "failed: a_ctrl %pK", a_ctrl);
		return -EINVAL;
	}

	soc_private =
		(struct cam_actuator_soc_private *)a_ctrl->soc_info.soc_private;
	power_info = &soc_private->power_info;
	soc_info = &a_ctrl->soc_info;

	if (!power_info) {
		CAM_ERR(CAM_ACTUATOR, "failed: power_info %pK", power_info);
		return -EINVAL;
	}
	rc = msm_camera_power_down(power_info, soc_info);
	if (rc) {
		CAM_ERR(CAM_ACTUATOR, "power down the core is failed:%d", rc);
		return rc;
	}

	camera_io_release(&a_ctrl->io_master_info);

	return rc;
}

static int32_t cam_actuator_i2c_modes_util(
	struct camera_io_master *io_master_info,
	struct i2c_settings_list *i2c_list)
{
	int32_t rc = 0;
	uint32_t i, size;

	if (i2c_list->op_code == CAM_SENSOR_I2C_WRITE_RANDOM) {
		rc = camera_io_dev_write(io_master_info,
			&(i2c_list->i2c_settings));
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR,
				"Failed to random write I2C settings: %d",
				rc);
			return rc;
		}
#ifdef CONFIG_MACH_SDM845_JUDYPN
		{
			struct cam_actuator_ctrl_t *a_ctrl = NULL;
			a_ctrl = container_of(io_master_info,
					struct cam_actuator_ctrl_t, io_master_info);
			msm_act_data_enqueue(i2c_list->i2c_settings.reg_setting[0].reg_addr,
					i2c_list->i2c_settings.reg_setting[0].reg_data, a_ctrl);
		}
#endif

	} else if (i2c_list->op_code == CAM_SENSOR_I2C_WRITE_SEQ) {
		rc = camera_io_dev_write_continuous(
			io_master_info,
			&(i2c_list->i2c_settings),
			0);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR,
				"Failed to seq write I2C settings: %d",
				rc);
			return rc;
			}
	} else if (i2c_list->op_code == CAM_SENSOR_I2C_WRITE_BURST) {
		rc = camera_io_dev_write_continuous(
			io_master_info,
			&(i2c_list->i2c_settings),
			1);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR,
				"Failed to burst write I2C settings: %d",
				rc);
			return rc;
		}
	} else if (i2c_list->op_code == CAM_SENSOR_I2C_POLL) {
		size = i2c_list->i2c_settings.size;
		for (i = 0; i < size; i++) {
			rc = camera_io_dev_poll(
			io_master_info,
			i2c_list->i2c_settings.reg_setting[i].reg_addr,
			i2c_list->i2c_settings.reg_setting[i].reg_data,
			i2c_list->i2c_settings.reg_setting[i].data_mask,
			i2c_list->i2c_settings.addr_type,
			i2c_list->i2c_settings.data_type,
			i2c_list->i2c_settings.reg_setting[i].delay);
			if (rc < 0) {
				CAM_ERR(CAM_ACTUATOR,
					"i2c poll apply setting Fail: %d", rc);
				return rc;
			}
		}
	}

	return rc;
}

int32_t cam_actuator_slaveInfo_pkt_parser(struct cam_actuator_ctrl_t *a_ctrl,
	uint32_t *cmd_buf)
{
	int32_t rc = 0;
	struct cam_cmd_i2c_info *i2c_info;

	if (!a_ctrl || !cmd_buf) {
		CAM_ERR(CAM_ACTUATOR, "Invalid Args");
		return -EINVAL;
	}

	i2c_info = (struct cam_cmd_i2c_info *)cmd_buf;
	if (a_ctrl->io_master_info.master_type == CCI_MASTER) {
		a_ctrl->io_master_info.cci_client->cci_i2c_master =
			a_ctrl->cci_i2c_master;
		a_ctrl->io_master_info.cci_client->i2c_freq_mode =
			i2c_info->i2c_freq_mode;
		a_ctrl->io_master_info.cci_client->sid =
			i2c_info->slave_addr >> 1;
		CAM_DBG(CAM_ACTUATOR, "Slave addr: 0x%x Freq Mode: %d",
			i2c_info->slave_addr, i2c_info->i2c_freq_mode);
	} else if (a_ctrl->io_master_info.master_type == I2C_MASTER) {
		a_ctrl->io_master_info.client->addr = i2c_info->slave_addr;
		CAM_DBG(CAM_ACTUATOR, "Slave addr: 0x%x", i2c_info->slave_addr);
	} else {
		CAM_ERR(CAM_ACTUATOR, "Invalid Master type: %d",
			a_ctrl->io_master_info.master_type);
		 rc = -EINVAL;
	}

	return rc;
}

int32_t cam_actuator_apply_settings(struct cam_actuator_ctrl_t *a_ctrl,
	struct i2c_settings_array *i2c_set)
{
	struct i2c_settings_list *i2c_list;
	int32_t rc = 0;

	if (a_ctrl == NULL || i2c_set == NULL) {
		CAM_ERR(CAM_ACTUATOR, "Invalid Args");
		return -EINVAL;
	}

	if (i2c_set->is_settings_valid != 1) {
		CAM_ERR(CAM_ACTUATOR, " Invalid settings");
		return -EINVAL;
	}

	list_for_each_entry(i2c_list,
		&(i2c_set->list_head), list) {
		rc = cam_actuator_i2c_modes_util(
			&(a_ctrl->io_master_info),
			i2c_list);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR,
				"Failed to apply settings: %d",
				rc);
			return rc;
		}
	}

	return rc;
}

int32_t cam_actuator_apply_request(struct cam_req_mgr_apply_request *apply)
{
	int32_t rc = 0, request_id, del_req_id;
	struct cam_actuator_ctrl_t *a_ctrl = NULL;

	if (!apply) {
		CAM_ERR(CAM_ACTUATOR, "Invalid Input Args");
		return -EINVAL;
	}

	a_ctrl = (struct cam_actuator_ctrl_t *)
		cam_get_device_priv(apply->dev_hdl);
	if (!a_ctrl) {
		CAM_ERR(CAM_ACTUATOR, "Device data is NULL");
		return -EINVAL;
	}
	request_id = apply->request_id % MAX_PER_FRAME_ARRAY;

	trace_cam_apply_req("Actuator", apply->request_id);

	CAM_DBG(CAM_ACTUATOR, "Request Id: %lld", apply->request_id);

	if ((apply->request_id ==
		a_ctrl->i2c_data.per_frame[request_id].request_id) &&
		(a_ctrl->i2c_data.per_frame[request_id].is_settings_valid)
		== 1) {
		rc = cam_actuator_apply_settings(a_ctrl,
			&a_ctrl->i2c_data.per_frame[request_id]);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR,
				"Failed in applying the request: %lld\n",
				apply->request_id);
			return rc;
		}
	}
	del_req_id = (request_id +
		MAX_PER_FRAME_ARRAY - MAX_SYSTEM_PIPELINE_DELAY) %
		MAX_PER_FRAME_ARRAY;

	if (apply->request_id >
		a_ctrl->i2c_data.per_frame[del_req_id].request_id) {
		a_ctrl->i2c_data.per_frame[del_req_id].request_id = 0;
		rc = delete_request(&a_ctrl->i2c_data.per_frame[del_req_id]);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR,
				"Fail deleting the req: %d err: %d\n",
				del_req_id, rc);
			return rc;
		}
	} else {
		CAM_DBG(CAM_ACTUATOR, "No Valid Req to clean Up");
	}

	return rc;
}

int32_t cam_actuator_establish_link(
	struct cam_req_mgr_core_dev_link_setup *link)
{
	struct cam_actuator_ctrl_t *a_ctrl = NULL;

	if (!link) {
		CAM_ERR(CAM_ACTUATOR, "Invalid Args");
		return -EINVAL;
	}

	a_ctrl = (struct cam_actuator_ctrl_t *)
		cam_get_device_priv(link->dev_hdl);
	if (!a_ctrl) {
		CAM_ERR(CAM_ACTUATOR, "Device data is NULL");
		return -EINVAL;
	}
	if (link->link_enable) {
		a_ctrl->bridge_intf.link_hdl = link->link_hdl;
		a_ctrl->bridge_intf.crm_cb = link->crm_cb;
	} else {
		a_ctrl->bridge_intf.link_hdl = -1;
		a_ctrl->bridge_intf.crm_cb = NULL;
	}

	return 0;
}

int32_t cam_actuator_publish_dev_info(struct cam_req_mgr_device_info *info)
{
	if (!info) {
		CAM_ERR(CAM_ACTUATOR, "Invalid Args");
		return -EINVAL;
	}

	info->dev_id = CAM_REQ_MGR_DEVICE_ACTUATOR;
	strlcpy(info->name, CAM_ACTUATOR_NAME, sizeof(info->name));
	info->p_delay = 0;
	info->trigger = CAM_TRIGGER_POINT_SOF;

	return 0;
}

int32_t cam_actuator_i2c_pkt_parse(struct cam_actuator_ctrl_t *a_ctrl,
	void *arg)
{
	int32_t  rc = 0;
	int32_t  i = 0;
	uint32_t total_cmd_buf_in_bytes = 0;
	size_t   len_of_buff = 0;
	uint32_t *offset = NULL;
	uint32_t *cmd_buf = NULL;
	uint64_t generic_ptr;
#ifdef CONFIG_MACH_SDM845_JUDYPN /*LGE_CHANGE, Enable Lens Temp. Correction Function, 2018-08-17, hongs.lee@lge.com */
	uint16_t ini_temp = 0;
#endif
	struct common_header      *cmm_hdr = NULL;
	struct cam_control        *ioctl_ctrl = NULL;
	struct cam_packet         *csl_packet = NULL;
	struct cam_config_dev_cmd config;
	struct i2c_data_settings  *i2c_data = NULL;
	struct i2c_settings_array *i2c_reg_settings = NULL;
	struct cam_cmd_buf_desc   *cmd_desc = NULL;
	struct cam_req_mgr_add_request  add_req;
	struct cam_actuator_soc_private *soc_private = NULL;
	struct cam_sensor_power_ctrl_t  *power_info = NULL;

	if (!a_ctrl || !arg) {
		CAM_ERR(CAM_ACTUATOR, "Invalid Args");
		return -EINVAL;
	}

	soc_private =
		(struct cam_actuator_soc_private *)a_ctrl->soc_info.soc_private;

	power_info = &soc_private->power_info;

	ioctl_ctrl = (struct cam_control *)arg;
	if (copy_from_user(&config, (void __user *) ioctl_ctrl->handle,
		sizeof(config)))
		return -EFAULT;
	rc = cam_mem_get_cpu_buf(config.packet_handle,
		(uint64_t *)&generic_ptr, &len_of_buff);
	if (rc < 0) {
		CAM_ERR(CAM_ACTUATOR, "Error in converting command Handle %d",
			rc);
		return rc;
	}

	if (config.offset > len_of_buff) {
		CAM_ERR(CAM_ACTUATOR,
			"offset is out of bounds: offset: %lld len: %zu",
			config.offset, len_of_buff);
		return -EINVAL;
	}

	csl_packet = (struct cam_packet *)(generic_ptr + config.offset);
	CAM_DBG(CAM_ACTUATOR, "Pkt opcode: %d", csl_packet->header.op_code);

	switch (csl_packet->header.op_code & 0xFFFFFF) {
	case CAM_ACTUATOR_PACKET_OPCODE_INIT:
		offset = (uint32_t *)&csl_packet->payload;
		offset += (csl_packet->cmd_buf_offset / sizeof(uint32_t));
		cmd_desc = (struct cam_cmd_buf_desc *)(offset);

		/* Loop through multiple command buffers */
		for (i = 0; i < csl_packet->num_cmd_buf; i++) {
			total_cmd_buf_in_bytes = cmd_desc[i].length;
			if (!total_cmd_buf_in_bytes)
				continue;
			rc = cam_mem_get_cpu_buf(cmd_desc[i].mem_handle,
					(uint64_t *)&generic_ptr, &len_of_buff);
			if (rc < 0) {
				CAM_ERR(CAM_ACTUATOR, "Failed to get cpu buf");
				return rc;
			}
			cmd_buf = (uint32_t *)generic_ptr;
			if (!cmd_buf) {
				CAM_ERR(CAM_ACTUATOR, "invalid cmd buf");
				return -EINVAL;
			}
			cmd_buf += cmd_desc[i].offset / sizeof(uint32_t);
			cmm_hdr = (struct common_header *)cmd_buf;

			switch (cmm_hdr->cmd_type) {
			case CAMERA_SENSOR_CMD_TYPE_I2C_INFO:
				CAM_DBG(CAM_ACTUATOR,
					"Received slave info buffer");
				rc = cam_actuator_slaveInfo_pkt_parser(
					a_ctrl, cmd_buf);
				if (rc < 0) {
					CAM_ERR(CAM_ACTUATOR,
					"Failed to parse slave info: %d", rc);
					return rc;
				}
				break;
			case CAMERA_SENSOR_CMD_TYPE_PWR_UP:
			case CAMERA_SENSOR_CMD_TYPE_PWR_DOWN:
				CAM_DBG(CAM_ACTUATOR,
					"Received power settings buffer");
				rc = cam_sensor_update_power_settings(
					cmd_buf,
					total_cmd_buf_in_bytes,
					power_info);
				if (rc) {
					CAM_ERR(CAM_ACTUATOR,
					"Failed:parse power settings: %d",
					rc);
					return rc;
				}
				break;
			default:
#ifdef CONFIG_MACH_SDM845_JUDYPN // QCT original , need to block code for judy
				CAM_DBG(CAM_ACTUATOR,
					"Received initSettings buffer");
				i2c_data = &(a_ctrl->i2c_data);
				i2c_reg_settings =
					&i2c_data->init_settings;

				i2c_reg_settings->request_id = 0;
				i2c_reg_settings->is_settings_valid = 1;
				rc = cam_sensor_i2c_command_parser(
					&a_ctrl->io_master_info,
					i2c_reg_settings,
					&cmd_desc[i], 1);
				if (rc < 0) {
					CAM_ERR(CAM_ACTUATOR,
					"Failed:parse init settings: %d",
					rc);
					return rc;
				}
#endif
				break;
			}
		}

		if (a_ctrl->cam_act_state == CAM_ACTUATOR_ACQUIRE) {
			rc = cam_actuator_power_up(a_ctrl);
			if (rc < 0) {
				CAM_ERR(CAM_ACTUATOR,
					" Actuator Power up failed");
				return rc;
			}
			a_ctrl->cam_act_state = CAM_ACTUATOR_CONFIG;
		}

#ifdef CONFIG_MACH_SDM845_JUDYPN
		/*LGE_CHANGE_S, Enable Lens Temp. Correction Function, 2018-08-17, hongs.lee@lge.com */
		// Enable Temperature Acquisition
		act_i2c_write(a_ctrl, 0x8E, 0x15, CAMERA_SENSOR_I2C_TYPE_BYTE);
		act_i2c_write(a_ctrl, 0x8D, 0x20, CAMERA_SENSOR_I2C_TYPE_BYTE);
		// Standby Release
		act_i2c_write(a_ctrl, 0x81, 0x80, CAMERA_SENSOR_I2C_TYPE_BYTE);
		usleep_range(1* 1000, 1* 1000 + 10); /* wait 1ms */
		// Read Initial Temperature Data
		act_i2c_read(a_ctrl, 0x58, &ini_temp, CAMERA_SENSOR_I2C_TYPE_WORD);
		/*LGE_CHANGE_E, Enable Lens Temp. Correction Function, 2018-08-17, hongs.lee@lge.com */

		rc = cam_actuator_apply_settings(a_ctrl,// QCT original
			&a_ctrl->i2c_data.init_settings);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR, "Cannot apply Init settings");
			return rc;
		}
		/*LGE_CHANGE_S, Enable Lens Temp. Correction Function, 2018-08-17, hongs.lee@lge.com */
		// Write Initial Temperature Data
		if(a_ctrl->io_master_info.master_type == I2C_MASTER) {
			CAM_ERR(CAM_ACTUATOR, "tele temp_ini %d", (int16_t)ini_temp);
			ini_temp = ini_temp << 3;
		}
		else {
			CAM_ERR(CAM_ACTUATOR, "main temp_ini %d", (int16_t)ini_temp);
		}
		act_i2c_write(a_ctrl, 0x30, ini_temp, CAMERA_SENSOR_I2C_TYPE_WORD);
		act_i2c_write(a_ctrl, 0x32, ini_temp, CAMERA_SENSOR_I2C_TYPE_WORD);
		act_i2c_write(a_ctrl, 0x76, ini_temp, CAMERA_SENSOR_I2C_TYPE_WORD);
		act_i2c_write(a_ctrl, 0x78, ini_temp, CAMERA_SENSOR_I2C_TYPE_WORD);
		// Enable Lens Temp. Correction Function
		act_i2c_write(a_ctrl, 0x8C, 0xE9, CAMERA_SENSOR_I2C_TYPE_BYTE);
		/*LGE_CHANGE_E, Enable Lens Temp. Correction Function, 2018-08-17, hongs.lee@lge.com */
#else
        if(a_ctrl->i2c_data.init_settings.is_settings_valid == 1){ // need to block code for judy, (LGE)
            rc = cam_actuator_apply_settings(a_ctrl,
                &a_ctrl->i2c_data.init_settings);
            if (rc < 0) {
                CAM_ERR(CAM_ACTUATOR, "Cannot apply Init settings");
                return rc;
            }
        }
#endif

		/* Delete the request even if the apply is failed */
		rc = delete_request(&a_ctrl->i2c_data.init_settings);
		if (rc < 0) {
			CAM_WARN(CAM_ACTUATOR,
				"Fail in deleting the Init settings");
			rc = 0;
		}
		break;
	case CAM_ACTUATOR_PACKET_AUTO_MOVE_LENS:
		if (a_ctrl->cam_act_state < CAM_ACTUATOR_CONFIG) {
			rc = -EINVAL;
			CAM_WARN(CAM_ACTUATOR,
				"Not in right state to move lens: %d",
				a_ctrl->cam_act_state);
			return rc;
		}
		a_ctrl->setting_apply_state = ACT_APPLY_SETTINGS_NOW;

		i2c_data = &(a_ctrl->i2c_data);
		i2c_reg_settings = &i2c_data->init_settings;

		i2c_data->init_settings.request_id =
			csl_packet->header.request_id;
		i2c_reg_settings->is_settings_valid = 1;
		offset = (uint32_t *)&csl_packet->payload;
		offset += csl_packet->cmd_buf_offset / sizeof(uint32_t);
		cmd_desc = (struct cam_cmd_buf_desc *)(offset);
		rc = cam_sensor_i2c_command_parser(
			&a_ctrl->io_master_info,
			i2c_reg_settings,
			cmd_desc, 1);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR,
				"Auto move lens parsing failed: %d", rc);
			return rc;
		}
		break;
	case CAM_ACTUATOR_PACKET_MANUAL_MOVE_LENS:
		if (a_ctrl->cam_act_state < CAM_ACTUATOR_CONFIG) {
			rc = -EINVAL;
			CAM_WARN(CAM_ACTUATOR,
				"Not in right state to move lens: %d",
				a_ctrl->cam_act_state);
			return rc;
		}
		i2c_data = &(a_ctrl->i2c_data);
		i2c_reg_settings = &i2c_data->per_frame[
			csl_packet->header.request_id % MAX_PER_FRAME_ARRAY];

		i2c_data->init_settings.request_id =
			csl_packet->header.request_id;
		i2c_reg_settings->is_settings_valid = 1;
		offset = (uint32_t *)&csl_packet->payload;
		offset += csl_packet->cmd_buf_offset / sizeof(uint32_t);
		cmd_desc = (struct cam_cmd_buf_desc *)(offset);
		rc = cam_sensor_i2c_command_parser(
			&a_ctrl->io_master_info,
			i2c_reg_settings,
			cmd_desc, 1);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR,
				"Manual move lens parsing failed: %d", rc);
			return rc;
		}
		break;
	}

	if ((csl_packet->header.op_code & 0xFFFFFF) !=
		CAM_ACTUATOR_PACKET_OPCODE_INIT) {
		add_req.link_hdl = a_ctrl->bridge_intf.link_hdl;
		add_req.req_id = csl_packet->header.request_id;
		add_req.dev_hdl = a_ctrl->bridge_intf.device_hdl;
		add_req.skip_before_applying = 0;
		if (a_ctrl->bridge_intf.crm_cb &&
			a_ctrl->bridge_intf.crm_cb->add_req)
			a_ctrl->bridge_intf.crm_cb->add_req(&add_req);
		CAM_DBG(CAM_ACTUATOR, "Req Id: %lld added to Bridge",
			add_req.req_id);
	}

	return rc;
}

void cam_actuator_shutdown(struct cam_actuator_ctrl_t *a_ctrl)
{
	int rc = 0;
	struct cam_actuator_soc_private  *soc_private =
		(struct cam_actuator_soc_private *)a_ctrl->soc_info.soc_private;
	struct cam_sensor_power_ctrl_t *power_info =
		&soc_private->power_info;

	if (a_ctrl->cam_act_state == CAM_ACTUATOR_INIT)
		return;

	if (a_ctrl->cam_act_state >= CAM_ACTUATOR_CONFIG) {
		rc = cam_actuator_power_down(a_ctrl);
		if (rc < 0)
			CAM_ERR(CAM_ACTUATOR, "Actuator Power down failed");
		a_ctrl->cam_act_state = CAM_ACTUATOR_ACQUIRE;
	}

	if (a_ctrl->cam_act_state >= CAM_ACTUATOR_ACQUIRE) {
		rc = cam_destroy_device_hdl(a_ctrl->bridge_intf.device_hdl);
		if (rc < 0)
			CAM_ERR(CAM_ACTUATOR, "destroying  dhdl failed");
		a_ctrl->bridge_intf.device_hdl = -1;
		a_ctrl->bridge_intf.link_hdl = -1;
		a_ctrl->bridge_intf.session_hdl = -1;
	}

	kfree(power_info->power_setting);
	kfree(power_info->power_down_setting);
	power_info->power_setting = NULL;
	power_info->power_down_setting = NULL;

	a_ctrl->cam_act_state = CAM_ACTUATOR_INIT;
}

int32_t cam_actuator_driver_cmd(struct cam_actuator_ctrl_t *a_ctrl,
	void *arg)
{
	int rc = 0;
	struct cam_control *cmd = (struct cam_control *)arg;

	if (!a_ctrl || !cmd) {
		CAM_ERR(CAM_ACTUATOR, " Invalid Args");
		return -EINVAL;
	}

	if (cmd->handle_type != CAM_HANDLE_USER_POINTER) {
		CAM_ERR(CAM_ACTUATOR, "Invalid handle type: %d",
			cmd->handle_type);
		return -EINVAL;
	}

	CAM_DBG(CAM_ACTUATOR, "Opcode to Actuator: %d", cmd->op_code);

	mutex_lock(&(a_ctrl->actuator_mutex));
	switch (cmd->op_code) {
	case CAM_ACQUIRE_DEV: {
		struct cam_sensor_acquire_dev actuator_acq_dev;
		struct cam_create_dev_hdl bridge_params;

		if (a_ctrl->bridge_intf.device_hdl != -1) {
			CAM_ERR(CAM_ACTUATOR, "Device is already acquired");
			rc = -EINVAL;
			goto release_mutex;
		}
		rc = copy_from_user(&actuator_acq_dev,
			(void __user *) cmd->handle,
			sizeof(actuator_acq_dev));
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR, "Failed Copying from user\n");
			goto release_mutex;
		}

		bridge_params.session_hdl = actuator_acq_dev.session_handle;
		bridge_params.ops = &a_ctrl->bridge_intf.ops;
		bridge_params.v4l2_sub_dev_flag = 0;
		bridge_params.media_entity_flag = 0;
		bridge_params.priv = a_ctrl;

		actuator_acq_dev.device_handle =
			cam_create_device_hdl(&bridge_params);
		a_ctrl->bridge_intf.device_hdl = actuator_acq_dev.device_handle;
		a_ctrl->bridge_intf.session_hdl =
			actuator_acq_dev.session_handle;

		CAM_DBG(CAM_ACTUATOR, "Device Handle: %d",
			actuator_acq_dev.device_handle);
		if (copy_to_user((void __user *) cmd->handle, &actuator_acq_dev,
			sizeof(struct cam_sensor_acquire_dev))) {
			CAM_ERR(CAM_ACTUATOR, "Failed Copy to User");
			rc = -EFAULT;
			goto release_mutex;
		}

		a_ctrl->cam_act_state = CAM_ACTUATOR_ACQUIRE;
	}
		break;
	case CAM_RELEASE_DEV: {
		if (a_ctrl->cam_act_state == CAM_ACTUATOR_START) {
			rc = -EINVAL;
			CAM_WARN(CAM_ACTUATOR,
				"Cant release actuator: in start state");
			goto release_mutex;
		}

		if (a_ctrl->cam_act_state == CAM_ACTUATOR_CONFIG) {
			rc = cam_actuator_power_down(a_ctrl);
			if (rc < 0) {
				CAM_ERR(CAM_ACTUATOR,
					"Actuator Power down failed");
				goto release_mutex;
			}
		}

		if (a_ctrl->bridge_intf.device_hdl == -1) {
			CAM_ERR(CAM_ACTUATOR, "link hdl: %d device hdl: %d",
				a_ctrl->bridge_intf.device_hdl,
				a_ctrl->bridge_intf.link_hdl);
			rc = -EINVAL;
			goto release_mutex;
		}
		rc = cam_destroy_device_hdl(a_ctrl->bridge_intf.device_hdl);
		if (rc < 0)
			CAM_ERR(CAM_ACTUATOR, "destroying the device hdl");
		a_ctrl->bridge_intf.device_hdl = -1;
		a_ctrl->bridge_intf.link_hdl = -1;
		a_ctrl->bridge_intf.session_hdl = -1;
		a_ctrl->cam_act_state = CAM_ACTUATOR_INIT;
	}
		break;
	case CAM_QUERY_CAP: {
		struct cam_actuator_query_cap actuator_cap = {0};

		actuator_cap.slot_info = a_ctrl->soc_info.index;
		if (copy_to_user((void __user *) cmd->handle, &actuator_cap,
			sizeof(struct cam_actuator_query_cap))) {
			CAM_ERR(CAM_ACTUATOR, "Failed Copy to User");
			rc = -EFAULT;
			goto release_mutex;
		}
	}
		break;
	case CAM_START_DEV: {
		if (a_ctrl->cam_act_state != CAM_ACTUATOR_CONFIG) {
			rc = -EINVAL;
			CAM_WARN(CAM_ACTUATOR,
			"Not in right state to start : %d",
			a_ctrl->cam_act_state);
			goto release_mutex;
		}
		a_ctrl->cam_act_state = CAM_ACTUATOR_START;
	}
		break;
	case CAM_STOP_DEV: {
		struct i2c_settings_array *i2c_set = NULL;
		int i;

		if (a_ctrl->cam_act_state != CAM_ACTUATOR_START) {
			rc = -EINVAL;
			CAM_WARN(CAM_ACTUATOR,
			"Not in right state to stop : %d",
			a_ctrl->cam_act_state);
			goto release_mutex;
		}

#ifdef CONFIG_MACH_SDM845_JUDYPN
		// AF Hall buffer reset
		memset(&(a_ctrl->buf), 0, sizeof(struct msm_act_readout_buffer));
#endif

		for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
			i2c_set = &(a_ctrl->i2c_data.per_frame[i]);

			if (i2c_set->is_settings_valid == 1) {
				rc = delete_request(i2c_set);
				if (rc < 0)
					CAM_ERR(CAM_SENSOR,
						"delete request: %lld rc: %d",
						i2c_set->request_id, rc);
			}
		}
		a_ctrl->cam_act_state = CAM_ACTUATOR_CONFIG;
	}
		break;
	case CAM_CONFIG_DEV: {
		a_ctrl->setting_apply_state =
			ACT_APPLY_SETTINGS_LATER;
		rc = cam_actuator_i2c_pkt_parse(a_ctrl, arg);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR, "Failed in actuator Parsing");
			goto release_mutex;
		}

		if (a_ctrl->setting_apply_state ==
			ACT_APPLY_SETTINGS_NOW) {
			rc = cam_actuator_apply_settings(a_ctrl,
				&a_ctrl->i2c_data.init_settings);
			if (rc < 0)
				CAM_ERR(CAM_ACTUATOR,
					"Cannot apply Update settings");

			/* Delete the request even if the apply is failed */
			rc = delete_request(&a_ctrl->i2c_data.init_settings);
			if (rc < 0) {
				CAM_ERR(CAM_ACTUATOR,
					"Failed in Deleting the Init Pkt: %d",
					rc);
				goto release_mutex;
			}
		}
	}
		break;
	default:
		CAM_ERR(CAM_ACTUATOR, "Invalid Opcode %d", cmd->op_code);
	}

release_mutex:
	mutex_unlock(&(a_ctrl->actuator_mutex));

	return rc;
}

int32_t cam_actuator_flush_request(struct cam_req_mgr_flush_request *flush_req)
{
	int32_t rc = 0, i;
	uint32_t cancel_req_id_found = 0;
	struct cam_actuator_ctrl_t *a_ctrl = NULL;
	struct i2c_settings_array *i2c_set = NULL;

	if (!flush_req)
		return -EINVAL;

	a_ctrl = (struct cam_actuator_ctrl_t *)
		cam_get_device_priv(flush_req->dev_hdl);
	if (!a_ctrl) {
		CAM_ERR(CAM_ACTUATOR, "Device data is NULL");
		return -EINVAL;
	}

	for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
		i2c_set = &(a_ctrl->i2c_data.per_frame[i]);

		if ((flush_req->type == CAM_REQ_MGR_FLUSH_TYPE_CANCEL_REQ)
				&& (i2c_set->request_id != flush_req->req_id))
			continue;

		if (i2c_set->is_settings_valid == 1) {
			rc = delete_request(i2c_set);
			if (rc < 0)
				CAM_ERR(CAM_ACTUATOR,
					"delete request: %lld rc: %d",
					i2c_set->request_id, rc);

			if (flush_req->type ==
				CAM_REQ_MGR_FLUSH_TYPE_CANCEL_REQ) {
				cancel_req_id_found = 1;
				break;
			}
		}
	}

	if (flush_req->type == CAM_REQ_MGR_FLUSH_TYPE_CANCEL_REQ &&
		!cancel_req_id_found)
		CAM_DBG(CAM_ACTUATOR,
			"Flush request id:%lld not found in the pending list",
			flush_req->req_id);
	return rc;
}

#ifdef CONFIG_MACH_SDM845_JUDYPN /*LGE_CHANGE, Enable Lens Temp. Correction Function, 2018-08-17, hongs.lee@lge.com */
int32_t act_i2c_read(struct cam_actuator_ctrl_t *a_ctrl, uint32_t RamAddr, uint16_t *RamData, enum camera_sensor_i2c_type data_type)
{
	int32_t ret = 0;
	uint32_t data = 0;

	ret = camera_io_dev_read(
		&(a_ctrl->io_master_info),
		RamAddr,
		&data,
		CAMERA_SENSOR_I2C_TYPE_BYTE,
		data_type);

	*RamData = (uint16_t)data;

	return ret;
}

int32_t act_i2c_write(struct cam_actuator_ctrl_t *a_ctrl, uint32_t RamAddr, uint32_t RamData, enum camera_sensor_i2c_type data_type)
{
	struct cam_sensor_i2c_reg_setting  i2c_reg_setting;
	int32_t ret = 0;

	i2c_reg_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	i2c_reg_setting.data_type = data_type;
	i2c_reg_setting.size = 1;
	i2c_reg_setting.delay = 0;
	i2c_reg_setting.reg_setting = (struct cam_sensor_i2c_reg_array *)
		kzalloc(sizeof(struct cam_sensor_i2c_reg_array) * 1, GFP_KERNEL);
	if (i2c_reg_setting.reg_setting == NULL)
	{
		CAM_ERR(CAM_ACTUATOR,"kzalloc failed");
		ret = -1;
		return ret;
	}

	i2c_reg_setting.reg_setting->reg_addr = RamAddr;
	i2c_reg_setting.reg_setting->reg_data = RamData;
	i2c_reg_setting.reg_setting->delay = 0;
	i2c_reg_setting.reg_setting->data_mask = 0;

	ret = camera_io_dev_write(
		&(a_ctrl->io_master_info),
		&i2c_reg_setting);
	kfree(i2c_reg_setting.reg_setting);
	return ret;
}
#endif
