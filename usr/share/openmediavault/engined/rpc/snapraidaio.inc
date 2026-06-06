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
        $this->registerMethod("get");
        $this->registerMethod("set");

        $this->registerMethod("getConfigList");
        $this->registerMethod("getConfig");
        $this->registerMethod("setConfig");
        $this->registerMethod("deleteConfig");
        $this->registerMethod("enumerateConfigFiles");
        $this->registerMethod("getContainers");
    }

    public function get($params, $context)
    {
        // Validate the RPC caller context.
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        // Get the configuration object.
        $db = \OMV\Config\Database::getInstance();
        $object = $db->get("conf.service.snapraidaio");
        // Remove useless properties from the object.
        $object->remove("configs");
        return $object->getAssoc();
    }

    public function set($params, $context)
    {
        // Validate the RPC caller context.
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        // Validate the parameters of the RPC service method.
        $this->validateMethodParams($params, "rpc.snapraidaio.set");
        // Get the existing configuration object.
        $db = \OMV\Config\Database::getInstance();
        $object = $db->get("conf.service.snapraidaio");
        $object->setAssoc($params);
        $db->set($object);
        // Remove useless properties from the object.
        $object->remove("configs");
        // Return the configuration object.
        return $object->getAssoc();
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
        $this->validateMethodParams($params, "rpc.snapraidaio.setconfig");
        // Prepare the configuration object.
        $object = new \OMV\Config\ConfigObject("conf.service.snapraidaio.config");
        $db = \OMV\Config\Database::getInstance();
        $object->setAssoc($params);
        // Set the configuration object.
        $isNew = $object->isNew();
        $db = \OMV\Config\Database::getInstance();
        if (TRUE === $isNew) {
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
        $db->delete($object);
        // Return the deleted configuration object.
        return $object->getAssoc();
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
}
