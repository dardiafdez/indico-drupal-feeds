<?php
define('INDICO_URL', 'https://indico.cern.ch');

class IndicoFetcherResult extends FeedsFetcherResult {
    protected $url;
    protected $file_path;

    public function __construct($url = NULL) {
        $this->url = $url;
        parent::__construct('');
    }

    public function getRaw() {
        feeds_include_library('http_request.inc', 'http_request');
        $result = http_request_get($this->url, null, null, true);
        if (!in_array($result->code, array(200, 201, 202, 203, 204, 205, 206))) {
            if(isset($result->data)) {
                $response = json_decode($result->data);
                $msg = 'Import failed: ' . $response->message;
            }
            else {
                $msg = t('Download of @url failed with code !code.', array('@url' => $this->url, '!code' => $result->code));
            }
            throw new Exception($msg);
        }
        return $this->sanitizeRaw($result->data);
    }
}


class IndicoFetcher extends FeedsFetcher {
    protected function makeUrl($source_config) {
        $categories = $source_config['categories'];
        $date_from = $source_config['date_from'];
        $date_to = $source_config['date_to'];
        $only_public = $source_config['only_public'];
        $room = $source_config['room'];

        $params = array();
        if(!$categories) {
            $categories = '0'; // top-level category -> all categories
        }
        if($date_from) {
            $params['from'] = $date_from;
        }
        if($date_to) {
            $params['to'] = $date_to;
        }
        if($room) {
            $params['room'] = $room;
        }
        if($only_public) {
            $params['onlypublic'] = 'yes';
        }

        $url = '/export/categ/' . str_replace(',', '-', $categories) . '.json?';
        $secret = $this->config['secret_key'];
        $params['ak'] = $this->config['api_key'];
        $params['timestamp'] = time();
        uksort($params, 'strcasecmp');
        $url .= http_build_query($params);
        if ($secret) {
            $signature = hash_hmac('sha1', $url, $secret);
            $url .= '&signature=' . $signature;
        }
        return INDICO_URL . $url;
    }

    public function fetch(FeedsSource $source) {
        $source_config = $source->getConfigFor($this);
        return new IndicoFetcherResult($this->makeUrl($source_config));
    }

    public function configDefaults() {
        return array(
            'api_key' => '',
            'secret_key' => '',
        );
    }

    public function configForm(&$form_state) {
        $form = array();
        $form['api_key'] = array(
            '#type' => 'textfield',
            '#title' => t('API Key'),
            '#default_value' => $this->config['api_key'],
            '#size' => 40,
            '#maxlength' => 36,
            '#description' => t('The API key for your Indico user. See <em><a href="http://indico.cern.ch">Indico</a> &raquo; My Profile &raquo; HTTP API</em> for further information.'),
            '#required' => true,
        );
        $form['secret_key'] = array(
            '#type' => 'textfield',
            '#title' => t('Secret Key'),
            '#default_value' => $this->config['secret_key'],
            '#size' => 40,
            '#maxlength' => 36,
            '#description' => t('The secret key for your Indico user.'),
            '#required' => false,
        );
        return $form;
    }

    public function configFormValidate(&$values) {
        $UUID_re = '/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/';
        if ($values['api_key'] && !preg_match($UUID_re, $values['api_key'])) {
            form_set_error('feeds][api_key', t('The API key has an invalid format.'));
        }

        $secret_key = $values['secret_key'];
        if ($secret_key && !preg_match($UUID_re, $secret_key)) {
            form_set_error('feeds][secret_key', t('The secret key has an invalid format.'));
        }
    }

    public function sourceDefaults() {
        return array(
            'categories' => '',
            'date_from' => 'today',
            'date_to' => 'today',
            'room' => '',
            'only_public' => true,
        );
    }

    public function sourceForm($source_config) {
        $form = array();
        $form['categories'] = array(
            '#type' => 'textfield',
            '#title' => t('Categories'),
            '#default_value' => isset($source_config['categories']) ? $source_config['categories'] : '',
            '#description' => t('Category ID(s) to retrieve events from. Separate multiple IDs with commas. To find out the ID of a category, go to the category in <a href="http://indico.cern.ch">Indico</a> and copy the numeric value next to <em>/category/</em> from the URL.'),
            '#required' => false,
            '#maxlength' => 1024,
        );

        $form['date_from'] = array(
            '#type' => 'textfield',
            '#title' => t('Start date'),
            '#default_value' => isset($source_config['date_from']) ? $source_config['date_from'] : '',
            '#description' => t("The earliest event date in YYYY-MM-DD form. You can also use '+-Xd' to specify an offset based on the current date (e.g. '-2d' for events since 2 days ago). 'today', 'yesterday' and 'tomorrow' are also accepted."),
            '#required' => false,
        );

        $form['date_to'] = array(
            '#type' => 'textfield',
            '#title' => t('End date'),
            '#default_value' => isset($source_config['date_to']) ? $source_config['date_to'] : '',
            '#description' => t("The latest event date in YYYY-MM-DD form. You can also use '+Xd' to specify an offset based on the start date. 'today' and 'tomorrow' are also accepted."),
            '#required' => false,
        );

        $form['room'] = array(
            '#type' => 'textfield',
            '#title' => t('Room'),
            '#default_value' => isset($source_config['room']) ? $source_config['room'] : '',
            '#description' => t("The room where the event must take place. You may use <strong>*</strong> as a wildcard."),
            '#required' => false,
        );

        $form['only_public'] = array(
            '#type' => 'checkbox',
            '#title' => t('Retrieve only public events'),
            '#default_value' => isset($source_config['only_public']) ? $source_config['only_public'] : false,
            '#description' => t("Only events visible to anonymous users are retrieved."),
            '#required' => false,
        );

        return $form;
    }

    private function _validateDate(&$date, $isEndDate) {
        $date = trim($date);
        if(!$date) {
            return true;
        }
        elseif($date == 'today' || $date == 'tomorrow') {
            return true;
        }
        elseif(!$isEndDate && $date == 'yesterday') {
            return true;
        }
        elseif(preg_match('/^([+-])?\s*(\d{1,3})\s*(?:d(?:ays)?)?$/', $date, $m)) {
            if(!$m[1]) {
                $m[1] = '+';
            }
            if($m[1] == '-' && $isEndDate) {
                // end date cannot be specified as today - x days
                return false;
            }
            if((int)$m[2]) {
                $date = $m[1] . $m[2] . 'd';
                return true;
            }
        }
        elseif(preg_match('/^\d{4}-\d{2}-\d{2}$/', $date) && strtotime($date)) {
            return true;
        }
        return false;
    }

    public function sourceFormValidate(&$values) {
        $categories = $values['categories'];
        if (preg_match_all('/\w+/', $categories, $matches)) {
            $values['categories'] = implode(',', $matches[0]);
        }
        else {
            $values['categories'] = '';
        }

        if(!$this->_validateDate($values['date_from'], false)) {
            form_set_error('feeds][date_from', t('The start date has an invalid format.'));
        }
        if(!$this->_validateDate($values['date_to'], true)) {
            form_set_error('feeds][date_to', t('The end date has an invalid format.'));
        }

        if(!$values['categories'] && !$values['date_from'] && !$values['date_to'] && !$values['room']) {
            form_set_error('feeds', t('You need to specify some filter criteria.'));
        }
    }

    public function configFormSubmit(&$values) {
        parent::configFormSubmit($values);
        drupal_set_message(t('To import data from indico you might now want to <a href="@url">create a new <em>Indico Feed</em> content node</a>.', array('@url' => url('node/add/indico-feed'))));
        if($values['secret_key']) {
            drupal_set_message(t('By providing a secret key, protected events may be imported. <strong>You are required to ensure that only authorized users are able to access information about these events!</strong>'), 'warning');
        }
    }
}

class IndicoParser extends FeedsParser {
    public function getMappingSources() {
        return array(
            'id' => array(
                'name' => t('Event ID'),
                'description' => t('Unique ID of the event.'),
            ),
            'type' => array(
                'name' => t('Type'),
                'description' => t('Type of the event'),
            ),
            'title' => array(
                'name' => t('Title'),
                'description' => t('Title of the event'),
            ),
            'description' => array(
                'name' => t('Description'),
                'description' => t('Description of the event'),
            ),
            'startDateTime' => array(
                'name' => t('Start date/time'),
                'description' => t('Start date/time of the event'),
            ),
            'endDateTime' => array(
                'name' => t('End date/time'),
                'description' => t('End date/time of the event'),
            ),
            'timezone' => array(
                'name' => t('Timezone'),
                'description' => t('Timezone of the event'),
            ),
            'location' => array(
                'name' => t('Location'),
                'description' => t('Location of the event'),
            ),
            'room' => array(
                'name' => t('Room'),
                'description' => t('Room of the event'),
            ),
            'category' => array(
                'name' => t('Category'),
                'description' => t('Indico category of the event'),
            ),
            'categoryId' => array(
                'name' => t('Category ID'),
                'description' => t('Indico category ID of the event'),
            ),
            'roomMapURL' => array(
                'name' => t('Room Map URL'),
                'description' => t('Link to a map showing the room'),
            ),
            'link' => array(
                'name' => t('Indico link'),
                'description' => t('Indico link of the event'),
            ),
            'eventChair' => array(
                'name' => t('Event Chairs'),
                'description' => t('Event Chairs from the event'),
            ),
            'iCal' => array(
                'name' => t('iCal link'),
                'description' => t('Indico iCal link of the event'),
            ),
        );
    }

    public function parse(FeedsSource $source, FeedsFetcherResult $fetcher_result) {
        $data = json_decode($fetcher_result->getRaw());
        $result = new FeedsParserResult();
        foreach($data->results as $event) {
            $startDT = new DateObject($event->startDate->date.' '.$event->startDate->time, $event->startDate->tz, DATE_FORMAT_DATETIME);
            $endDT = new DateObject($event->endDate->date.' '.$event->endDate->time, $event->endDate->tz, DATE_FORMAT_DATETIME);
            $chairs = "";
            foreach ($event->chairs as $subarr) {
                $tmparr = explode(",",$subarr->fullName);
                $titles = "/Mr.|Ms.|Mrs.|Dr.|Prof./";
                if (preg_match($titles, $tmparr[0], $matches)) {
                        $tmparr[0] = preg_replace($titles,"",$tmparr[0]);
                        $tmparr[1] = $matches[0]." ".$tmparr[1];
                }
                if (!isset($chairs))
                        $chairs = $tmparr[1]." ".$tmparr[0];
                else
                        $chairs = $chairs.", ". $tmparr[1]." ".$tmparr[0];
            }
            $item = array(
                'id' => $event->id,
                'type' => $event->type,
                'title' => strip_tags($event->title),
                'description' => $event->description,
                'startDateTime' => $startDT,
                'endDateTime' => $endDT,
                'timezone' => $event->timezone,
                'location' => $event->location,
                'room' => $event->room,
                'category' => $event->category,
                'categoryId' => $event->categoryId,
                'roomMapURL' => $event->roomMapURL,
                'link' => $event->url,
                'eventChair' => $chairs,
                'iCal' => $this->_get_ical_link($event->url),
            );
            $result->items[] = $item;
        }
        return $result;
    }

    private function _get_ical_link($url) {
        if (preg_match("/event\/([\d]+)\/?$/i", $url, $match)) {
            return INDICO_URL . '/export/event/' . $match[1] . '.ics';
        }
        return $url;
    }
}
