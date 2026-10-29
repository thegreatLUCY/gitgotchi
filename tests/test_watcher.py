import importlib.util, pathlib, subprocess, tempfile, unittest
spec=importlib.util.spec_from_file_location('companion',pathlib.Path(__file__).parents[1]/'companion/app.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class WatcherTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=pathlib.Path(self.temp.name);self.repo=self.root/'repo';self.repo.mkdir()
        self.git('init','-q');self.git('config','user.name','Test');self.git('config','user.email','test@example.invalid');self.commit('first')
        self.events=[];self.w=module.Watcher(self.repo,self.root/'state.json',self.events.append)
    def tearDown(self):self.temp.cleanup()
    def git(self,*args):return subprocess.check_output(['git','-C',str(self.repo),*args],text=True).strip()
    def commit(self,text):
        (self.repo/'file').write_text(text);self.git('add','file');self.git('commit','-qm',text)
    def test_baseline_and_restart_do_not_feed(self):
        self.assertIsNone(self.w.poll());self.assertIsNone(self.w.poll());self.assertFalse(self.events)
        self.commit('second');self.assertEqual(self.w.poll()['kind'],'feed')
        restarted=module.Watcher(self.repo,self.root/'state.json',self.events.append)
        self.assertIsNone(restarted.poll());self.assertEqual(self.events,['feed'])
    def test_daily_cap_and_backward_reset(self):
        self.w.poll();first=self.git('rev-parse','HEAD')
        for i in range(5):self.commit(str(i));self.w.poll()
        self.assertEqual(len(self.events),3)
        self.git('reset','--hard',first);self.assertIsNone(self.w.poll())
    def test_empty_commit_does_not_feed(self):
        self.w.poll();self.git('commit','--allow-empty','-qm','empty');self.assertIsNone(self.w.poll())
if __name__=='__main__':unittest.main()
