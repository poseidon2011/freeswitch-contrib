<?php
/**
 * @package  FS_CURL
 * @subpackage FS_CURL_Configuration
 * fs_curl.php
 */
if (basename($_SERVER['PHP_SELF']) == basename(__FILE__)) {
    header('Location: index.php');
}

/**
 * @package FS_CURL
 * @license
 * @author Raymond Chandler (intralanman) <intralanman@gmail.com>
 * @version 0.1
 * FreeSWITCH CURL base class
 * Base class for all curl XML output, contains methods for XML output and
 * connecting to a database
 * @return void
*/
class fs_curl {
    /**
     * MDB2 Object
     * @link http://pear.php.net/MDB2
     * @var $db MDB2
     */
    public $db;
    /**
     * Array of _REQUEST parameters passed
     *
     * @var array
     */
    public $request;
    /**
     * XMLWriter object
     * @link http://php.net/XMLWriter
     * @var object
     */
    public $xmlw;
    /**
     * Array of comments to be output in the XML
     * @see fs_curl::comment
     * @var array
     */
    private $comments;

    /**
     * Instantiation of XMLWriter and MDB2
     * This method will instantiate the MDB2 and XMLWriter classes for use
     * in child classes
     * @return void
    */
    public function fs_curl() {
        header('Content-Type: text/xml');
        $this -> open_xml();
        $this -> generate_request_array();
        $inc = array('required'=>'MDB2.php');
        $this -> include_files($inc);
        $this -> connect_db(DEFAULT_DSN);
        set_error_handler(array($this, 'error_handler'));
        //trigger_error('blah', E_USER_ERROR);
    }

    /**
     * Connect to a database via MDB2
     * @param mixed $dsn data source for database connection (array or string)
     * @return void
    */
    public function connect_db($dsn) {
        $this -> db = MDB2::connect($dsn);
        if (MDB2::isError($this -> db)) {
            $this -> comment($this -> db -> getMessage());
            $this -> file_not_found();
        }
        $this -> db -> setFetchMode(MDB2_FETCHMODE_ASSOC);
    }

    /**
     * Method to add comments to XML
     * Adds a comment to be displayed in the final XML
     * @param string $comment comment string to be output in XML
     * @return void
    */
    public function comment($comment) {
        $this -> comments[] = $comment;
    }

    /**
     * Generate a globally accesible array of the _REQUEST parameters passed
     * Generates an array from the _REQUEST parameters that were passed, keeping
     * all key => value combinations intact
     * @return void
    */
    private function generate_request_array() {
        while (list($req_key, $req_val) = each($_REQUEST)) {
            //$this -> comment("$req_key => $req_val");
            $this -> request[$req_key] = $req_val;
        }
    }

    /**
     * Actual Instantiation of XMLWriter Object
     * This method creates an XMLWriter Object and sets some needed options
     * @return void
    */
    private function open_xml() {
        $this -> xmlw = new XMLWriter();
        $this -> xmlw -> openMemory();
        $this -> xmlw -> setIndent(true);
        $this -> xmlw -> setIndentString('  ');
        $this -> xmlw -> startDocument('1.0', 'UTF-8', 'no');
        //set the freeswitch document type
        $this -> xmlw -> startElement('document');
        $this -> xmlw -> writeAttribute('type', 'freeswitch/xml');
    }

    /**
     * Method to call on any error that can not be revovered from
     * This method was written to return a valid XML response to FreeSWITCH
     * in the event that we are unable to generate a valid configuration file
     * from the passed information
     * @return void
    */
    public function file_not_found() {
        $this -> comment('Include Path = ' . ini_get('include_path'));
        $not_found = new XMLWriter();
        $not_found -> openMemory();
        $not_found -> setIndent(true);
        $not_found -> setIndentString('  ');
        $not_found -> startDocument('1.0', 'UTF-8', 'no');
        //set the freeswitch document type
        $not_found -> startElement('document');
        $not_found -> writeAttribute('type', 'freeswitch/xml');
        $not_found -> startElement('section');
        $not_found -> writeAttribute('name', 'result');
        $not_found -> startElement('result');
        $not_found -> writeAttribute('status', 'not found');
        $not_found -> endElement();
        $not_found -> endElement();
        $not_found -> endElement();
        $this -> comments2xml($not_found, $this -> comments);
        echo $not_found -> outputMemory();
        exit();
    }

    /**
     * Generate XML comments from comments array
     * This [recursive] method will iterate over the passed array, writing XML
     * comments and calling itself in the event that the "comment" is an array
     * @param object $xml_obj Already instantiated XMLWriter object
     * @param array $comments [Multi-dementional] Array of comments to be added
     * @param integer $space_pad Number of spaces to indent the comments
     * @return void
    */
    private function comments2xml($xml_obj, $comments, $space_pad=0) {
        $comment_count = count($comments);
        for ($i = 0; $i < $comment_count; $i++) {
            if (!is_array($comments[$i])) {
                $xml_obj -> writeComment($comments[$i]);
            } else {
                $this -> comments2xml($xml_obj, $comments[$i], $space_pad + 2);
            }
        }
    }

    /**
     * End open XML elments in XMLWriter object
     * @return void
    */
    private function close_xml() {
        $this -> xmlw -> endElement();
        $this -> xmlw -> endElement();
        $this -> xmlw -> endElement();
    }

    /**
     * Close and Output XML and stop script execution
     * @return void
    */
    public function output_xml() {
        $this -> close_xml();
        $comment_count = count($this -> comments);
        for ($i = 0; $i < $comment_count; $i++) {
            $this -> xmlw -> writeComment($this -> comments[$i]);
        }
        echo $this -> xmlw -> outputMemory();
        exit();
    }

    /**
     * Recursive method to add an array of comments
     * @return void
    */
    public function comment_array($array, $spacepad=0) {
        $spaces = str_repeat(' ', $spacepad);
        foreach ($array as $key => $val) {
            if (is_array($val)) {
                $this -> comment("$spaces$key => Array");
                $this -> comment_array($val, $spacepad+2);
            } else {
            	$this -> comment("$spaces$key => $val");
            }
        }
    }

    /**
     * Include files for child classes
     * This method will include the files needed by child classes.
     * Expects an associative array of type => path
     * where type = [required|any other string]
     * @param array $file_array associative array of files to include
     * @return void
     * @todo add other types for different levels of errors
    */
    public function include_files($file_array) {
        $return = FS_CURL_SUCCESS;
        while (list($type, $file) = each($file_array)) {
            $inc = @include_once($file);
            if (!$inc) {
                $comment = sprintf(
                'Unable To Include %s File %s', $type, $file
                );
                $this -> comment($comment);
                if ($type == 'required') {
                    $return = FS_CURL_CRITICAL;
                } else {
                    if ($return != FS_CURL_CRITICAL) {
                        $return = FS_CURL_WARNING;
                    }
                }
            }
        }
        if ($return == FS_CURL_CRITICAL) {
            $this -> file_not_found();
        }
        return $return;
    }

    /**
     * Class-wide error handler
     * This method should be called whenever there is an error in any child
     * classes, script execution and returning is pariatlly determined by
     * defines
     * @see RETURN_ON_WARN
     * @return void
     * @todo add other defines that control what, if any, comments gets output
    */
    public function error_handler($no, $str, $file, $line) {
        /*
        $this -> comment("USER_ERROR " . E_USER_ERROR);
        $this -> comment("USER_NOTICE " . E_USER_NOTICE);
        $this -> comment("USER_WARNING " . E_USER_WARNING);
        $this -> comment("ALL " . E_ALL);
        $this -> comment("COMPILE_ERROR " . E_COMPILE_ERROR);
        $this -> comment("COMPILE_WARNING " . E_COMPILE_WARNING);
        $this -> comment("CORE_ERROR " . E_CORE_ERROR);
        $this -> comment("CORE_WARNING " . E_CORE_WARNING);
        $this -> comment("ERROR " . E_ERROR);
        $this -> comment("NOTICE " . E_NOTICE);
        $this -> comment("WARNING " . E_WARNING);
        $this -> comment("PARSE " . E_PARSE);
        $this -> comment("RECOVERABLE_ERROR " . E_RECOVERABLE_ERROR);
        $this -> comment("STRICT " . E_STRICT);
        //$this -> comment(E);
        */
        if ($no == E_STRICT) {
            return true;
        }

        $file = ereg_replace('\.(inc|php)$', '', $file);
        $this -> comment(basename($file) . ":$line - $no:$str");

        switch ($no) {
            case E_USER_NOTICE:
            case E_NOTICE:
                break;
            case E_USER_WARNING:
            case E_WARNING:
                if (defined('RETURN_ON_WARN') && RETURN_ON_WARN == true) {
                    break;
                }
            case E_ERROR:
            case E_USER_ERROR:
            default:
                $this -> file_not_found();
        }
        return true;
    }
}
?>