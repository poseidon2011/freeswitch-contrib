<?xml version="1.0"?>
<document type="freeswitch/xml">
  <section name="dialplan" description="Regex/XML Dialplan">
    <context name="default">
      <extension name="myextension">
      <condition field="destination_number" expression="^.*$">
{foreach key=k item=v from=$ACTION}
          <action application="{$k}" data="{$v}"/>
{/foreach}
        </condition>
      </extension>
    </context>
  </section>
</document>
