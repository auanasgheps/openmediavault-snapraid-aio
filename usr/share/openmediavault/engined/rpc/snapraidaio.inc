<?php
/*
 * @author    OpenMediaVault Plugin Developers <plugins@omv-extras.org>
 * @copyright Copyright (c) 2025-2026 openmediavault plugin developers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

require_once("openmediavault/functions.inc");

class OMVRpcServiceSnapRaidAio extends \OMV\Rpc\ServiceAbstract
{
    public function getName()
    {
        return "SnapRaidAio";
    }

    public function initialize()
    {
        $this->registerMethod("getConfigList");
        $this->registerMethod("getConfig");
        $this->registerMethod("setConfig");
        $this->registerMethod("deleteConfig");
        $this->registerMethod("enumerateConfigFiles");
        $this->registerMethod("getContainers");
        $this->registerMethod("getSchedule");
        $this->registerMethod("setSchedule");
    }

    public function getConfigList($params, $context)
    {
        // Validate the RPC caller context.
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        // Validate the parameters of the RPC service method.
        $this->validateMethodParams($params, "rpc.common.getlist");
        // Get configuration data.
        $db = \OMV\Config\Database::getInstance();
        $objects = $db->getAssoc("conf.service.snapraidaio.config");
        // Filter the result.
        return $this->applyFilter($objects, $params["start"], $params["limit"],
            $params["sortfield"], $params["sortdir"]);
    }

    public function getConfig($params, $context)
    {
        // Validate the RPC caller context.
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        // Validate the parameters of the RPC service method.
        $this->validateMethodParams($params, "rpc.common.objectuuid");
        // Get the configuration object.
        $db = \OMV\Config\Database::getInstance();
        $result = $db->getAssoc("conf.service.snapraidaio.config", $params["uuid"]);
        $result["services"] = strlen($result["services"]) ? explode(",", $result["services"]) : [];
        $emailCfg = $db->getAssoc("conf.system.notification.email");
        $result["email_sender"]   = $emailCfg["sender"]       ?? "";
        $result["email_receiver"] = $emailCfg["primaryemail"] ?? "";
        return $result;
    }

    public function setConfig($params, $context)
    {
        // Validate the RPC caller context.
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        // Validate the parameters of the RPC service method.
        if (isset($params["services"]) && $params["services"] !== null && $params["services"] !== "null") {
            $params["services"] = implode(",", $params["services"]);
        } else {
            $params["services"] = "";
        }
        unset($params["email_sender"]);
        unset($params["email_receiver"]);
        $this->validateMethodParams($params, "rpc.snapraidaio.setconfig");
        $db = \OMV\Config\Database::getInstance();
        // Load existing object to preserve fields not sent by the form (e.g. cron_uuid).
        $isNew = ($params["uuid"] === \OMV\Environment::get("OMV_CONFIGOBJECT_NEW_UUID"));
        if ($isNew) {
            $object = new \OMV\Config\ConfigObject("conf.service.snapraidaio.config");
        } else {
            $object = $db->get("conf.service.snapraidaio.config", $params["uuid"]);
        }
        $object->setAssoc($params);
        if ($isNew) {
            // Check uniqueness - config file
            $db->assertIsUnique($object, "snapraid_conf");
        }
        $db->set($object);
        // Return the configuration object.
        return $object->getAssoc();
    }

    public function deleteConfig($params, $context)
    {
        // Validate the RPC caller context.
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        // Validate the parameters of the RPC service method.
        $this->validateMethodParams($params, "rpc.common.objectuuid");
        // Delete the configuration object.
        $db = \OMV\Config\Database::getInstance();
        $object = $db->get("conf.service.snapraidaio.config", $params["uuid"]);
        $assoc = $object->getAssoc();
        $db->delete($object);
        // Remove the generated AIO conf file if it exists.
        $confFile = sprintf("/etc/snapraid-aio-%s.conf", $params["uuid"]);
        if (file_exists($confFile)) {
            unlink($confFile);
        }
        // Remove the linked cron entry if one was saved.
        if (!empty($assoc["cron_uuid"])) {
            \OMV\Rpc\Rpc::call("Cron", "delete",
                ["uuid" => $assoc["cron_uuid"]], $context);
        }
        // Return the deleted configuration object.
        return $assoc;
    }

    public function enumerateConfigFiles($params, $context)
    {
        // Validate the RPC caller context.
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        $dir = "/etc/snapraid";
        // check if /etc/snapraid exists
        if (!is_dir($dir)) {
            throw new \OMV\Exception(gettext("/etc/snapraid does not exist!"));
        }
        // get all snapraid config files
        $files = [];
        $files[] = [
            "path" => "/etc/snapraid.conf"
        ];
        $it = new DirectoryIterator($dir);
        foreach ($it as $entry) {
            if ($entry->isDot()) continue;
            if (!$entry->isFile()) continue;
            if ($entry->isLink()) continue;
            $path = $entry->getPathname();
            if (!preg_match("/\.conf$/i", $path)) continue;
            $files[] = [
                "path" => $path
            ];
        }
        return ($files);
    }

    public function getContainers($params, $context)
    {
        // Validate the RPC caller context.
        $this->validateMethodContext($context, ["role" => OMV_ROLE_EVERYONE]);
        $objects = [];
        $status = "";
        // check to see if docker is installed and running
        if (file_exists("/lib/systemd/system/docker.service")) {
            $systemCtl = new \OMV\System\SystemCtl("docker");
            $running = $systemCtl->isActive();
            $status = $running ? gettext("running") : gettext("stopped");
        } else {
            $running = false;
            $status = gettext("not installed");
        }
        // get containers and status if docker is running
        $objects = [];
        if ($running) {
            $output = [];
            $cmdArgs = [];
            $cmdArgs[] = "docker";
            $cmdArgs[] = "container";
            $cmdArgs[] = "ls";
            $cmdArgs[] = '--format "{{.Image}},{{.Names}},{{.Status}}"';
            $cmd = new \OMV\System\Process($cmdArgs);
            $cmd->execute($output, $exitStatus);
            foreach ($output as $cnt) {
                if (empty($cnt)) {
                    continue;
                }
                $parts = explode(",", $cnt);
                $objects[] = [
                    "image" => $parts[0],
                    "name" => $parts[1],
                    "status" => $parts[2]
                ];
            }
        }
        return ($objects);
    }

    public function getSchedule($params, $context)
    {
        // Validate the RPC caller context.
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        // Validate the parameters of the RPC service method.
        $this->validateMethodParams($params, "rpc.common.objectuuid");
        $configUuid = $params["uuid"];
        $db = \OMV\Config\Database::getInstance();
        $configObject = $db->get("conf.service.snapraidaio.config", $configUuid);
        $cronUuid = $configObject->get("cron_uuid");
        if (!empty($cronUuid)) {
            try {
                $cron = \OMV\Rpc\Rpc::call("Cron", "get", ["uuid" => $cronUuid], $context);
                // Replace the cron UUID with the config UUID so the form POSTs back
                // to the right config, and carry the cron UUID separately.
                $cron["uuid"]      = $configUuid;
                $cron["cron_uuid"] = $cronUuid;
                // Cron::get returns time fields as arrays for 'exactly' execution;
                // flatten to scalars for the form's single-select fields.
                foreach (["minute", "hour", "dayofmonth", "month", "dayofweek"] as $field) {
                    if (isset($cron[$field]) && is_array($cron[$field])) {
                        $cron[$field] = implode(",", $cron[$field]);
                    }
                }
                return $cron;
            } catch (\Exception $e) {
                // Cron entry no longer exists; clear stale UUID and fall through.
                $configObject->set("cron_uuid", "");
                $db->set($configObject);
            }
        }
        // No cron entry yet; return defaults. uuid = config UUID so the form
        // POSTs back correctly; cron_uuid is empty to signal first-time save.
        return [
            "uuid"       => $configUuid,
            "cron_uuid"  => "",
            "enable"     => true,
            "execution"  => "exactly",
            "minute"     => "0",
            "hour"       => "0",
            "dayofmonth" => "*",
            "month"      => "*",
            "dayofweek"  => "*",
            "sendemail"  => false,
            "comment"    => "",
        ];
    }

    public function setSchedule($params, $context)
    {
        // Validate the RPC caller context.
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        // Validate the parameters of the RPC service method.
        $this->validateMethodParams($params, "rpc.common.objectuuid");
        $configUuid = $params["uuid"];
        // cron_uuid is passed back from the form (set by getSchedule).
        $cronUuid = isset($params["cron_uuid"]) ? $params["cron_uuid"] : "";
        $db = \OMV\Config\Database::getInstance();
        $configObject = $db->get("conf.service.snapraidaio.config", $configUuid);
        // Build the Cron RPC params. Strip fields unknown to the Cron schema.
        $cronParams = $params;
        unset($cronParams["cron_uuid"]);
        $cronParams["uuid"] = !empty($cronUuid)
            ? $cronUuid
            : \OMV\Environment::get("OMV_CONFIGOBJECT_NEW_UUID");
        $cronParams["type"]     = "userdefined";
        $cronParams["username"] = "root";
        $cronParams["command"]  = sprintf(
            "/usr/sbin/snapraid-aio-script.sh -c /etc/snapraid-aio-%s.conf",
            $configUuid
        );
        // Required by the Cron schema; we always use fixed values, not "every N".
        $cronParams["everynminute"]     = false;
        $cronParams["everynhour"]       = false;
        $cronParams["everyndayofmonth"] = false;
        // Cron::set expects time fields as arrays for 'exactly' execution.
        foreach (["minute", "hour", "dayofmonth", "month", "dayofweek"] as $field) {
            if (isset($cronParams[$field]) && !is_array($cronParams[$field])) {
                $cronParams[$field] = [$cronParams[$field]];
            }
        }
        // Save the cron entry and retrieve its UUID (new on first save).
        try {
            $cronResult = \OMV\Rpc\Rpc::call("Cron", "set", $cronParams, $context);
        } catch (\Exception $e) {
            if (empty($cronUuid)) {
                throw $e;
            }
            // Stale UUID — the cron entry no longer exists; create a new one instead.
            $cronUuid = "";
            $cronParams["uuid"] = \OMV\Environment::get("OMV_CONFIGOBJECT_NEW_UUID");
            $cronResult = \OMV\Rpc\Rpc::call("Cron", "set", $cronParams, $context);
        }
        // Persist the cron entry UUID back to the config on first save.
        if (empty($cronUuid)) {
            $configObject->set("cron_uuid", $cronResult["uuid"]);
            $db->set($configObject);
        }
        return $cronResult;
    }
}
