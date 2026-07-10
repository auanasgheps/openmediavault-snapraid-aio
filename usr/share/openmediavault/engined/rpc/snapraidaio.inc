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
        $this->registerMethod("executeCommand");
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
        try {
            $labelMap = $this->buildLabelMap();
        } catch (\Exception $e) {
            $labelMap = [];
        }
        $uuids = array_column($objects, 'uuid');
        try {
            $lastRunMap = $this->getLastRunStatuses($uuids);
        } catch (\Exception $e) {
            $lastRunMap = array_fill_keys($uuids, '');
        }
        foreach ($objects as &$obj) {
            $obj["array_name"] = $labelMap[$obj["snapraid_conf"]] ?? "";
            if (!empty($obj["cron_uuid"])) {
                try {
                    \OMV\Rpc\Rpc::call("Cron", "get", ["uuid" => $obj["cron_uuid"]], $context);
                    $obj["scheduled"] = true;
                } catch (\Exception $e) {
                    // Cron entry no longer exists; report unscheduled without
                    // writing back to the DB to avoid triggering an Apply banner.
                    $obj["scheduled"] = false;
                }
            } else {
                $obj["scheduled"] = false;
            }
            $obj["last_run"] = $lastRunMap[$obj["uuid"]] ?? '';
        }
        unset($obj);
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
        $db = \OMV\Config\Database::getInstance();
        $isNew = ($params["uuid"] === \OMV\Environment::get("OMV_CONFIGOBJECT_NEW_UUID"));
        if ($isNew) {
            // Return datamodel defaults for a new object.
            $object = new \OMV\Config\ConfigObject("conf.service.snapraidaio.config");
            $result = $object->getAssoc();
            // Ensure the sentinel UUID is returned so the form can POST it back.
            $result["uuid"]     = $params["uuid"];
            $result["services"] = [];
        } else {
            $result = $db->getAssoc("conf.service.snapraidaio.config", $params["uuid"]);
            $result["services"] = strlen($result["services"]) ? explode(",", $result["services"]) : [];
        }
        $emailCfg = $db->getAssoc("conf.system.notification.email");
        $result["email_sender"]   = $emailCfg["sender"]       ?? "";
        $result["email_receiver"] = $emailCfg["primaryemail"] ?? "";
        return $result;
    }

    public function setConfig($params, $context)
    {
        // Validate the RPC caller context.
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        // Normalize empty uuid (sent by confObjUuid in edit mode for new objects)
        // to the sentinel so validateMethodParams and isNew checks work correctly.
        if (empty($params["uuid"])) {
            $params["uuid"] = \OMV\Environment::get("OMV_CONFIGOBJECT_NEW_UUID");
        }
        // Validate the parameters of the RPC service method.
        if (isset($params["services"]) && is_array($params["services"])) {
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
        $confFile = $this->getAioConfPath($params["uuid"]);
        if (file_exists($confFile)) {
            unlink($confFile);
        }
        // Remove per-array state files (warn/scrub counters).
        foreach (["warnCount", "scrubCount"] as $suffix) {
            $stateFile = sprintf("/var/lib/snapraidaio/%s.%s", $params["uuid"], $suffix);
            if (file_exists($stateFile)) {
                unlink($stateFile);
            }
        }
        // Remove the linked cron entry if one was saved.
        if (!empty($assoc["cron_uuid"])) {
            try {
                \OMV\Rpc\Rpc::call("Cron", "delete",
                    ["uuid" => $assoc["cron_uuid"]], $context);
            } catch (\Exception $e) {
                // Cron entry already gone; nothing to do.
            }
        }
        // Return the deleted configuration object.
        return $assoc;
    }

    public function enumerateConfigFiles($params, $context)
    {
        // Validate the RPC caller context.
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        try {
            $labelMap = $this->buildLabelMap();
        } catch (\Exception $e) {
            $labelMap = [];
        }
        $files = [];
        foreach ($labelMap as $path => $label) {
            $files[] = ["path" => $path, "label" => $label];
        }
        usort($files, fn($a, $b) => strcasecmp($a["label"], $b["label"]));
        return $files;
    }

    public function getContainers($params, $context)
    {
        // Validate the RPC caller context.
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
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

    private function getAioConfPath($uuid)
    {
        return sprintf("/etc/snapraid-aio-%s.conf", $uuid);
    }

    // Returns ["uuid" => "YYYY-MM-DD HH:MM — OK|WARNING|Started"] for each UUID.
    // Reads /var/log/snapraid.log once and scans for the last run start per UUID.
    // "Started" means the run began but no completion line was found — could be
    // currently running or an aborted/failed run.
    private function getLastRunStatuses($uuids)
    {
        $result = array_fill_keys($uuids, '');
        $logFile = "/var/log/snapraid.log";
        if (!is_readable($logFile)) {
            return $result;
        }
        $lines = file($logFile, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES);
        if ($lines === false) {
            return $result;
        }
        $uuidSet = array_flip($uuids);
        // Forward pass: record index of last start line seen for each UUID.
        $lastStartIdx = [];
        $lastStartTs  = [];
        foreach ($lines as $i => $line) {
            if (strpos($line, 'Using configuration file: /etc/snapraid-aio-') === false) {
                continue;
            }
            if (!preg_match('~snapraid-aio-([0-9a-f\-]{36})\.conf~', $line, $m)) {
                continue;
            }
            $uuid = $m[1];
            if (!isset($uuidSet[$uuid])) {
                continue;
            }
            $lastStartIdx[$uuid] = $i;
            // Timestamp is the leading "[YYYY-MM-DD HH:MM" portion of the line.
            $ts = '';
            if (preg_match('/^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2})/', $line, $tm)) {
                $ts = $tm[1];
            }
            $lastStartTs[$uuid] = $ts;
        }
        // For each UUID with a known start, scan its run range for completion/warnings.
        $total = count($lines);
        foreach ($lastStartIdx as $uuid => $startIdx) {
            // Find end boundary: next start line after $startIdx, or EOF.
            $endIdx = $total;
            for ($j = $startIdx + 1; $j < $total; $j++) {
                if (strpos($lines[$j], 'Using configuration file: /etc/snapraid-aio-') !== false) {
                    $endIdx = $j;
                    break;
                }
            }
            $completed = false;
            $hasWarn   = false;
            for ($j = $startIdx + 1; $j < $endIdx; $j++) {
                if (!$completed && strpos($lines[$j], 'Total time elapsed for SnapRAID') !== false) {
                    $completed = true;
                }
                if (!$hasWarn && strpos($lines[$j], ': WARN: \'') !== false) {
                    $hasWarn = true;
                }
            }
            if (!$completed) {
                $status = 'Started';
            } elseif ($hasWarn) {
                $status = 'WARNING';
            } else {
                $status = 'OK';
            }
            $ts = $lastStartTs[$uuid];
            $result[$uuid] = $ts ? sprintf("%s \u{2014} %s", $ts, $status) : $status;
        }
        return $result;
    }

    // Build a [realpath => label] map from /etc/snapraid/ symlinks.
    private function buildLabelMap()
    {
        $dir = "/etc/snapraid";
        if (!is_dir($dir)) {
            throw new \OMV\Exception(gettext("/etc/snapraid does not exist!"));
        }
        $map = [];
        $it = new DirectoryIterator($dir);
        foreach ($it as $entry) {
            if ($entry->isDot()) continue;
            if (!$entry->isLink()) continue;
            $path = $entry->getPathname();
            if (!preg_match("/\.conf$/i", $path)) continue;
            $real = realpath($path);
            if ($real === false) continue;
            $map[$real] = basename($path, ".conf");
        }
        return $map;
    }

    private function getArrayLabel($confPath)
    {
        try {
            $map = $this->buildLabelMap();
        } catch (\Exception $e) {
            return "";
        }
        return $map[$confPath] ?? "";
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
                // Cron entry no longer exists; fall through to defaults without
                // writing back to the DB to avoid triggering an Apply banner.
            }
        }
        // No cron entry yet; return defaults. uuid = config UUID so the form
        // POSTs back correctly; cron_uuid is empty to signal first-time save.
        $label = $this->getArrayLabel($configObject->get("snapraid_conf"));
        $comment = $label ? sprintf("SnapRAID AIO %s", $label) : "SnapRAID AIO";
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
            "comment"    => $comment,
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
        $isNewCron = empty($cronUuid);
        $db = \OMV\Config\Database::getInstance();
        $configObject = $db->get("conf.service.snapraidaio.config", $configUuid);
        // Build the Cron RPC params. Strip fields unknown to the Cron schema.
        $cronParams = $params;
        unset($cronParams["cron_uuid"]);
        $cronParams["uuid"] = !$isNewCron
            ? $cronUuid
            : \OMV\Environment::get("OMV_CONFIGOBJECT_NEW_UUID");
        $cronParams["type"]     = "userdefined";
        $cronParams["username"] = "root";
        $cronParams["command"]  = sprintf(
            "/usr/sbin/snapraid-aio-script.sh --config %s",
            $this->getAioConfPath($configUuid)
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
            if ($isNewCron) {
                throw $e;
            }
            // Stale UUID — the cron entry no longer exists; create a new one instead.
            $isNewCron = true;
            $cronParams["uuid"] = \OMV\Environment::get("OMV_CONFIGOBJECT_NEW_UUID");
            $cronResult = \OMV\Rpc\Rpc::call("Cron", "set", $cronParams, $context);
        }
        // Persist the cron entry UUID back to the config on first save (or recovery).
        if ($isNewCron) {
            $configObject->set("cron_uuid", $cronResult["uuid"]);
            $db->set($configObject);
        }
        return $cronResult;
    }

    public function executeCommand($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        $this->validateMethodParams($params, '{
            "type": "object",
            "properties": {
                "uuid": { "type": "string" },
                "command": { "type": "string", "enum": ["sync", "force-sync", "run", "last-log"] }
            },
            "required": ["uuid", "command"]
        }');
        $db = \OMV\Config\Database::getInstance();
        $object = $db->get("conf.service.snapraidaio.config", $params['uuid']);
        $aioConf = $this->getAioConfPath($params['uuid']);
        return $this->execBgProc(function($bgStatusFilename, $bgOutputFilename)
                use ($object, $params, $aioConf) {
            switch ($params['command']) {
                case "run":
                    if (!file_exists($aioConf)) {
                        throw new \OMV\Exception(gettext(
                            "AIO config file not found. Please apply settings first."
                        ));
                    }
                    $cmdArgs = [
                        "/usr/sbin/snapraid-aio-script.sh",
                        "--config", $aioConf
                    ];
                    break;
                case "force-sync":
                    if (!file_exists($aioConf)) {
                        throw new \OMV\Exception(gettext(
                            "AIO config file not found. Please apply settings first."
                        ));
                    }
                    $cmdArgs = [
                        "/usr/sbin/snapraid-aio-script.sh",
                        "--config", $aioConf,
                        "--force-sync"
                    ];
                    break;
                case "sync":
                    $snapraidConf = $object->get("snapraid_conf");
                    if (empty($snapraidConf)) {
                        throw new \OMV\Exception(gettext(
                            "No SnapRAID array selected. Please edit the config and choose an array."
                        ));
                    }
                    $cmdArgs = [
                        "/usr/bin/snapraid",
                        "sync", "-c", $snapraidConf
                    ];
                    break;
                case "last-log":
                    $logDir = $object->get("snapraid_log_dir");
                    // Fall back to /root if empty or contains a shell variable
                    // (e.g. the legacy default "$HOME" which PHP cannot expand).
                    if (empty($logDir) || strpos($logDir, '$') !== false) {
                        $logDir = "/root";
                    }
                    $files = glob($logDir . "/SnapRAID-*.txt");
                    if (empty($files)) {
                        $cmdArgs = ["/bin/echo", "No log files found in " . $logDir . "."];
                    } else {
                        usort($files, fn($a, $b) => filemtime($b) - filemtime($a));
                        $cmdArgs = ["/bin/cat", $files[0]];
                    }
                    break;
            }
            $cmd = new \OMV\System\Process($cmdArgs);
            $cmd->setRedirect2to1();
            $cmdLine = $cmd->getCommandLine();
            $this->exec($cmdLine, $output, $bgOutputFilename);
            return $output;
        });
    }
}
