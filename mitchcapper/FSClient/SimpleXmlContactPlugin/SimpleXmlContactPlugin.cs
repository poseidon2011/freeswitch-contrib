﻿using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Xml.Serialization;
using FSClient;

namespace SimpleXmlContactPlugin {
	public class SimpleXmlContactPlugin : SimpleContactPluginBase {
		public override string ProviderName() {
			return "SimpleXmlContactPlugin";
		}
		public class XmlDictionary<T, V> : Dictionary<T, V>, IXmlSerializable {
			[XmlType("Entry")]
			public struct Entry {
				public Entry(T key, V value) : this() { Key = key; Value = value; }
				[XmlElement("Key")]
				public T Key { get; set; }
				[XmlElement("Value")]
				public V Value { get; set; }
			}

			System.Xml.Schema.XmlSchema IXmlSerializable.GetSchema() {
				return null;
			}

			void IXmlSerializable.ReadXml(System.Xml.XmlReader reader) {
				Clear();
				var serializer = new XmlSerializer(typeof(List<Entry>));
				reader.Read();
				var list = (List<Entry>)serializer.Deserialize(reader);
				foreach (var entry in list) Add(entry.Key, entry.Value);
				reader.ReadEndElement();
			}

			void IXmlSerializable.WriteXml(System.Xml.XmlWriter writer) {
				var list = new List<Entry>(Count);
				list.AddRange(this.Select(entry => new Entry(entry.Key, entry.Value)));
				XmlSerializer serializer = new XmlSerializer(list.GetType());
				serializer.Serialize(writer, list);
			}
		}


		protected override void _TryResolveNewNumber(string number, NumberResolved on_resolved)
		{
			
		}
		protected override string NormalizeNumber(String number)
		{
			if (number.Length == 11 && number.StartsWith("1"))
				number = number.Substring(1);

			return number;
		}
		XmlSerializer SerializerObj = new XmlSerializer(typeof(XmlDictionary<string, string>));
		private void SaveDatabase(){


			TextWriter WriteFileStream = new StreamWriter(Utils.GetUserDataPath() + "\\SimpleXmlContacts.xml");
			SerializerObj.Serialize(WriteFileStream, number_to_alias_ref);
			WriteFileStream.Close();

		}

		private XmlDictionary<string, string> number_to_alias_ref;
		protected override void LoadDatabase(ref Dictionary<string, string> number_to_alias_db)//alright so we hijack the database to convert it to be serializeable
		{
			number_to_alias_ref = new XmlDictionary<string, string>();
			try{
				using (FileStream ReadFileStream = new FileStream(Utils.GetUserDataPath() + "\\SimpleXmlContacts.xml", FileMode.Open, FileAccess.Read, FileShare.Read)) {
					number_to_alias_ref = (XmlDictionary<string, string>)SerializerObj.Deserialize(ReadFileStream);
				}
			}catch{}
			number_to_alias_db = number_to_alias_ref;
		}
		public override void Terminate()
		{

		}
		
		protected override string IsValidAlias(String str)//return null to abort updating, otherwire return string
		{
			if (String.IsNullOrEmpty(str))
				return null;
			return str;
		}
		
		protected override void UpdateDatabase(string number, string alias){
			SaveDatabase();
		}

	}
}
