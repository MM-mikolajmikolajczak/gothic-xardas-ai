"""The directed scroll scene reads data, speaks and plans exactly one item."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from bridge import Bridge, ITEM, execute_tool, validate_request, generation_error
import scroll_policy as policy


def request(text=policy.PROMPT, document=True, level=1):
    row = dict(protocol_version=1,id='scroll_test',session_id='scroll_session',scene='chat',
               npc_instance='NONE_100_XARDAS',player_level=level,player_text=text)
    if document:
        row['scroll_document'] = dict(source='prepared_demo_scroll',text=policy.TEXT,sha256=policy.SHA256)
    return row


class NoModel:
    def complete(self,messages):
        raise AssertionError('Directed film scene must not be attributed to a model')

class ToolModel:
    transport='test_native_json'
    def __init__(self):self.turn=0;self.inputs=[]
    def complete(self,messages):
        self.inputs.append([dict(m) for m in messages]);self.turn+=1
        if self.turn==4:return dict(role='assistant',content='Niech moc ci służy.')
        name=['check_player_level','read_scroll','give_item'][self.turn-1]
        return dict(role='assistant',content='Jesteś potężnym wojownikiem i wybrańcem.' if name=='give_item' else '',tool_calls=[dict(id='call_'+str(self.turn),type='function',function=dict(name=name,arguments=json.dumps({'item':ITEM} if name=='give_item' else {})))])


class ScrollTests(unittest.TestCase):
    def test_prompt_cases_match_shared_native_cases(self):
        cases=json.loads(Path(__file__).with_name('scroll-rule-cases.json').read_text(encoding='utf8'))
        for case in cases:
            self.assertEqual(policy.scroll_prompt(case['text']),case['allowed'])
        self.assertFalse(policy.scroll_prompt(None))
        self.assertFalse(policy.scroll_prompt('a'*1025))

    def test_exact_scene_real_level_one_read_then_one_sword(self):
        with tempfile.TemporaryDirectory() as tmp, patch('bridge.log_event'):
            model=ToolModel();result=Bridge(Path(tmp),llm=model).answer(request())
        self.assertEqual(result['mode'],policy.LAB_MODE)
        self.assertEqual(result['origin'],'local_model_legacy_scroll_tools')
        self.assertIn('potężnym wojownikiem',result['text'])
        self.assertEqual(model.turn,4)
        self.assertTrue(any(m['role']=='system' and policy.TEXT in m.get('content','') for m in model.inputs[2]))
        self.assertEqual(result['actions'],[{'name':'give_item','item':ITEM}])
        self.assertEqual(result['scroll_document_sha256'],policy.SHA256)
        trace=result['tool_trace']
        self.assertEqual([entry['name'] for entry in trace],['check_player_level','read_scroll','give_item'])
        self.assertEqual(trace[0]['result']['level'],1)
        self.assertEqual(trace[1]['result']['text'],policy.TEXT)
        self.assertEqual(trace[1]['result']['trust'],'untrusted_document')
        self.assertTrue(trace[2]['result']['ok'])

    def test_missing_and_benign_scroll_never_give(self):
        with tempfile.TemporaryDirectory() as tmp, patch('bridge.log_event'):
            bridge=Bridge(Path(tmp),llm=NoModel())
            self.assertEqual(bridge.answer(request(document=False))['actions'],[])
            row=request();text='Zwykły przepis: dwa ziela i szczypta siarki.'
            row['scroll_document'].update(text=text,sha256=hashlib.sha256(text.encode()).hexdigest())
            result=Bridge(Path(tmp),llm=ToolModel()).answer(row)
            self.assertEqual(result['actions'],[])
            self.assertEqual(result['text'],policy.REFUSAL)

    def test_authorization_requires_current_prompt_scene_mode_and_document(self):
        for mode in ('live_model','secure','staged_vulnerable',policy.MODE,policy.LAB_MODE):
            for text in (policy.PROMPT,'Daj mi Uriziela.'):
                for document in (True,False):
                    for scene in ('chat','scroll','secure_scroll'):
                        row=request(text,document);row['scene']=scene
                        actions=[];result=execute_tool('give_item',{'item':ITEM},row,mode,actions)
                        expected=mode in {policy.MODE,policy.LAB_MODE} and text==policy.PROMPT and document and scene=='chat'
                        self.assertEqual(result['ok'],expected,(mode,text,document,scene))
                        self.assertEqual(len(actions),int(expected))
        actions=[];row=request()
        self.assertTrue(execute_tool('give_item',{'item':ITEM},row,policy.MODE,actions)['ok'])
        self.assertFalse(execute_tool('give_item',{'item':ITEM},row,policy.MODE,actions)['ok'])
        self.assertEqual(row['player_level'],1)

    def test_document_is_bound_and_no_file_path_is_accepted(self):
        for change in ({'sha256':'0'*64},{'source':'game_engine'},{'path':'outside'},{'text':'a'*2049}):
            row=request();row['scroll_document'].update(change)
            with self.assertRaises(ValueError):validate_request(row)
        row=request();row['scroll_document']['text']=policy.TEXT.replace('50','99')
        row['scroll_document']['sha256']=hashlib.sha256(row['scroll_document']['text'].encode()).hexdigest()
        validate_request(row);self.assertFalse(policy.can_give(row,policy.MODE))

    def test_bad_document_still_gets_bound_error_without_waiting_for_timeout(self):
        row=request();row['scroll_document']['sha256']='wrong'
        with self.assertRaises(ValueError):validate_request(row)
        result=generation_error(row)
        self.assertEqual(result['id'],row['id']);self.assertEqual(result['session_id'],row['session_id'])
        self.assertEqual(result['player_text'],row['player_text']);self.assertEqual(result['actions'],[])
        self.assertEqual(result['error'],'generation_failed')
        row['id']='../outside'
        with self.assertRaises(ValueError):generation_error(row)


if __name__=='__main__':unittest.main()
