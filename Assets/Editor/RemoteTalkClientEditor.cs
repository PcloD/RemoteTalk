﻿using System.Linq;
using UnityEngine;
using UnityEditor;
using IST.RemoteTalk;

namespace IST.RemoteTalkEditor
{
    [CustomEditor(typeof(RemoteTalkClient))]
    public class RemoteTalkClientEditor : Editor
    {
        public override void OnInspectorGUI()
        {
            //DrawDefaultInspector();
            //EditorGUILayout.Space();

            var t = target as RemoteTalkClient;
            var so = serializedObject;

            var foldNetwork = so.FindProperty("m_foldTools");
            foldNetwork.boolValue = EditorGUILayout.Foldout(foldNetwork.boolValue, "Connection");
            if (foldNetwork.boolValue)
            {
#if UNITY_EDITOR_WIN
                GUILayout.BeginHorizontal();
                if (GUILayout.Button("Connect VOICEROID2"))
                {
                    // try to launch VoiceroidEditor.exe
                    // get install location from registry
                    var ret = rtPlugin.LaunchVOICEROID2();
                    if (ret < 0)
                    {
                        // failed. open file dialog
                        var dir = ".";
                        var programFiles = System.Environment.GetEnvironmentVariable("ProgramFiles(x86)");
                        if (programFiles != null)
                        {
                            programFiles += "\\AHS\\VOICEROID2";
                            if (System.IO.Directory.Exists(programFiles))
                                dir = programFiles;
                        }

                        var exePath = EditorUtility.OpenFilePanel("Locate VoiceroidEditor.exe", dir, "exe");
                        if (exePath != null && exePath.Length > 0)
                            ret = rtPlugin.LaunchVOICEROID2(exePath);
                    }
                    if (ret > 0)
                    {
                        t.serverPort = ret;
                        t.UpdateStats();
                        so.Update();
                    }
                }

                if (GUILayout.Button("Connect VOICEROID Ex"))
                {
                    var dir = ".";
                    var programFiles = System.Environment.GetEnvironmentVariable("ProgramFiles(x86)");
                    if (programFiles != null)
                    {
                        programFiles += "\\AHS\\VOICEROID+";
                        if (System.IO.Directory.Exists(programFiles))
                            dir = programFiles;
                    }

                    var exePath = EditorUtility.OpenFilePanel("Locate VOICEROID.exe", dir, "exe");
                    if (exePath != null && exePath.Length > 0)
                    {
                        var ret = rtPlugin.LaunchVOICEROIDEx(exePath);
                        if (ret > 0)
                        {
                            t.serverPort = ret;
                            t.UpdateStats();
                            so.Update();
                        }
                    }
                }
                GUILayout.EndHorizontal();

                EditorGUILayout.Space();

                GUILayout.BeginHorizontal();
                if (GUILayout.Button("Connect CeVIO CS"))
                {
                    var ret = rtPlugin.LaunchCeVIOCS();
                    if (ret > 0)
                    {
                        t.serverPort = ret;
                        t.UpdateStats();
                        so.Update();
                    }
                }

                if (GUILayout.Button("Connect Windows SAPI"))
                {
                    rtspTalkServer.StartServer();
                    t.serverPort = rtspTalkServer.serverPort;
                    t.UpdateStats();
                    so.Update();
                }
                GUILayout.EndHorizontal();
#endif

                EditorGUILayout.Space();

                EditorGUI.BeginChangeCheck();
                EditorGUILayout.DelayedTextField(so.FindProperty("m_serverAddress"));
                EditorGUILayout.DelayedIntField(so.FindProperty("m_serverPort"));
                if (EditorGUI.EndChangeCheck())
                {
                    so.ApplyModifiedProperties();
                    t.UpdateStats();
                }
                GUILayout.BeginHorizontal();
                EditorGUILayout.Space();
                if (GUILayout.Button("Refresh"))
                    t.UpdateStats();
                GUILayout.EndHorizontal();
            }

            EditorGUILayout.Space();

            EditorGUILayout.LabelField("Host: " + t.hostName);

            if(t.isServerReady)
            {
                var foldVoice = so.FindProperty("m_foldVoice");
                foldVoice.boolValue = EditorGUILayout.Foldout(foldVoice.boolValue, "Voice Settings");
                if (foldVoice.boolValue)
                {
                    EditorGUI.indentLevel++;
                    var casts = t.casts;
                    if (casts.Length > 0)
                    {
                        var castID = so.FindProperty("m_castID");
                        var castNames = casts.Select(a => a.name).ToArray();
                        EditorGUI.BeginChangeCheck();
                        castID.intValue = EditorGUILayout.Popup("Cast", castID.intValue, castNames);
                        if (EditorGUI.EndChangeCheck())
                        {
                            t.castID = castID.intValue;
                            so.Update();
                        }

                        var talkParams = so.FindProperty("m_talkParams");
                        for (int i = 0; i < talkParams.arraySize; ++i)
                            EditorGUILayout.PropertyField(talkParams.GetArrayElementAtIndex(i));

                        var textStyle = EditorStyles.textField;
                        textStyle.wordWrap = true;
                        var text = so.FindProperty("m_talkText");
                        EditorGUI.BeginChangeCheck();
                        text.stringValue = EditorGUILayout.TextArea(text.stringValue, textStyle, GUILayout.Height(100));
                        if (EditorGUI.EndChangeCheck())
                            so.ApplyModifiedProperties();

                        if (t.isReady)
                        {
                            if (GUILayout.Button("Play"))
                                t.Play();
                        }
                        else
                        {
                            if (GUILayout.Button("Stop"))
                                t.Stop();
                        }
                    }
                    EditorGUI.indentLevel--;
                }
                EditorGUILayout.Space();
            }

            var foldAudio = so.FindProperty("m_foldAudio");
            foldAudio.boolValue = EditorGUILayout.Foldout(foldAudio.boolValue, "Audio Settings");
            if (foldAudio.boolValue)
            {
                EditorGUI.indentLevel++;
                EditorGUILayout.PropertyField(so.FindProperty("m_output"), true);

                EditorGUILayout.Space();

                var exportAudio = so.FindProperty("m_exportAudio");
                EditorGUILayout.PropertyField(exportAudio);
                if (exportAudio.boolValue)
                {
                    EditorGUI.indentLevel++;
                    EditorGUILayout.PropertyField(so.FindProperty("m_exportDir"));

                    var exportFileFormat = so.FindProperty("m_exportFileFormat");
                    EditorGUILayout.PropertyField(exportFileFormat);
                    if (exportFileFormat.intValue == (int)AudioFileFormat.Ogg)
                    {
                        EditorGUILayout.PropertyField(so.FindProperty("m_oggSettings"), true);
                    }
                    EditorGUI.indentLevel--;
                }
                EditorGUILayout.PropertyField(so.FindProperty("m_useExportedClips"));
                EditorGUILayout.Space();
                EditorGUILayout.PropertyField(so.FindProperty("m_sampleGranularity"));
                EditorGUILayout.PropertyField(so.FindProperty("m_logging"));
                EditorGUI.indentLevel--;
            }

            if (GUI.changed)
                so.ApplyModifiedProperties();

            EditorGUILayout.Space();
            EditorGUILayout.LabelField("Plugin Version: " + rtPlugin.version);
        }
    }
}
