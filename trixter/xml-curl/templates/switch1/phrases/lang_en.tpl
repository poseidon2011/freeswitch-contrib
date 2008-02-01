<?xml version="1.0"?>
<document type="freeswitch/xml">
  <section name="phrases" description="Speech Phrase Management">
    <macros>
      <language name="en" sound_path="/snds" tts_engine="cepstral" tts_voice="david">
<include><!--This line will be ignored it's here to validate the xml and is optional -->
    <macro name="msgcount">
      <input pattern="(.*)">
	<match>
	  <action function="execute" data="sleep(1000)"/>
	  <action function="play-file" data="vm-youhave.wav"/>
	  <action function="say" data="$1" method="pronounced" type="items"/>
	  <action function="play-file" data="vm-messages.wav"/>
	  <!-- or -->
	  <!--<action function="speak-text" data="you have $1 messages"/>-->
	</match>
      </input>
    </macro>
    <macro name="saydate">
      <input pattern="(.*)">
	<match>
	  <action function="say" data="$1" method="pronounced" type="current_date_time"/>
	</match>
      </input>
    </macro>
    <macro name="timespec">
      <input pattern="(.*)">
	<match>
	  <action function="say" data="$1" method="pronounced" type="time_measurement"/>
	</match>
      </input>
    </macro>
    <macro name="ip-addr">
      <input pattern="(.*)">
	<match>
	  <action function="say" data="$1" method="iterated" type="ip_address"/>
	  <action function="say" data="$1" method="pronounced" type="ip_address"/>
	</match>
      </input>
    </macro>
    <macro name="spell">
      <input pattern="(.*)">
	<match>
	  <action function="say" data="$1" method="pronounced" type="name_spelled"/>
	</match>
      </input>
    </macro>
    <macro name="spell-phonetic">
      <input pattern="(.*)">
	<match>
	  <action function="say" data="$1" method="pronounced" type="name_phonetic"/>
	</match>
      </input>
    </macro>
    <macro name="tts-timeleft">
      <!-- The parser will visit each <input> tag and execute the actions in <match> or <nomatch> depending on the pattern param -->
      <!-- If the function "break" is encountered all parsing will cease -->
      <input pattern="(\d+):(\d+)">
	<match>
	  <action function="speak-text" data="You have $1 minutes, $2 seconds remaining $strftime(%Y-%m-%d)"/>
	  <action function="break"/>
	</match>
	<nomatch>
	  <action function="speak-text" data="That input was invalid."/>
	</nomatch>
      </input>
      <input pattern="(\d+) min (\d+) sec">
	<match>
	  <action function="speak-text" data="You have $1 minutes, $2 seconds remaining $strftime(%Y-%m-%d)"/>
	</match>
	<nomatch>
	  <action function="speak-text" data="That input was invalid."/>
	</nomatch>
      </input>
    </macro>
</include><!--This line will be ignored it's here to validate the xml and is optional -->
</language>
</macros>
</section>
</document>
