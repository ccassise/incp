from pathlib import Path
import asyncio
import os
import stat
import tempfile
import unittest

class TestIncp(unittest.IsolatedAsyncioTestCase):
    '''
    Tests conformance to the following with some exceptions:
    https://pubs.opengroup.org/onlinepubs/9699919799/utilities/cp.html#tag_20_24
    '''

    async def test_incp_src_dir_no_recurse_option(self):
        '''
        POSIX 2.a

        It should print an error message when source file is a directory and the
        recurse option was not set.
        '''
        self.skipTest('not implemented')
    
    async def test_incp_src_file_dot_dot(self):
        '''
        POSIX 2.b

        It should skip source file when source file is '.' or '..'.
        '''
        self.skipTest('not implemented')

    async def test_incp_src_file_dest_file_exists_but_not_file_or_dir(self):
        '''
        It should print an error message when destination file exists and is not
        a regular file or a directory.
        '''
        self.skipTest('not implemented')

    
    async def test_incp_src_dir_dest_file(self):
        '''
        POSIX 2.d

        It should print an error message when source file is a directory and
        destination file exists but is not a directory.
        '''
        self.skipTest('not implemented')

    # TODO: Check this with how cp works. POSIX 2.e.
    async def test_incp_src_dir_dest_dir_does_not_exist_no_recurse(self):
        '''
        POSIX 2.e

        It should create a directory with the same permissions as the source
        directory and then do nothing more when the recurse option is not
        selected.
        '''
        self.skipTest('not implemented')

    # TODO: Check this with how cp works. POSIX 2.e.
    async def test_incp_src_dir_dest_dir_does_not_exist_recurse(self):
        '''
        POSIX 2.e

        It should create a directory with the same permissions as the source
        directory and then copy all the contents of the source directory when
        the recurse option is set.
        '''
        self.skipTest('not implemented')

    async def test_incp_src_dir_dest_dir(self):
        '''
        POSIX 2.f

        It should copy the contents of the source directory to the destination
        directory when source and destination are directories.
        '''
        self.skipTest('not implemented')

    async def test_incp_src_dir_dest_file(self):
        '''
        POSIX 2.g

        It should set destination file bits to the same as source file when
        destination file is created.
        '''
        self.skipTest('not implemented')

    async def test_incp_src_file_does_not_exist(self):
        '''
        It should fail and write an error message when source file does not
        exist.
        '''
        receiver = await asyncio.create_subprocess_exec('./incp', '-l')
        await asyncio.sleep(0.5)
        sender = await asyncio.create_subprocess_exec('./incp', 'does_not_exist.txt', '127.0.0.1:also_does_not_exist.txt')
        await receiver.wait()
        await sender.wait()

        self.assertEqual(receiver.returncode, 0)
        self.assertEqual(sender.returncode, 1)
    
    async def test_incp_src_file_dest_file_exists(self):
        '''
        POSIX 3.a.ii

        It should truncate the file and write the contents of source file to
        destination file. It should not change the permission bits of the
        destination file.
        '''
        expected_text = b'hello, world\n'
        dir = tempfile.TemporaryDirectory()
        expected = Path.joinpath(Path(dir.name), 'expected.txt')
        actual = Path.joinpath(Path(dir.name), 'actual.txt')
        fin = open(expected.absolute(), 'wb')
        fin.write(expected_text)
        fin.close()
        os.chmod(expected.absolute(), stat.S_IREAD)
        fout = open(actual.absolute(), 'wb')
        fout.write(b'Test text that\n\t should be truncated')
        fout.close()
        os.chmod(actual.absolute(), stat.S_IREAD | stat.S_IWRITE)

        receiver = await asyncio.create_subprocess_exec('./incp', '-l', '4628')
        await asyncio.sleep(0.5)
        sender = await asyncio.create_subprocess_exec('./incp', expected.absolute(), f"127.0.0.1:4628:{actual.absolute()}")
        await receiver.wait()
        await sender.wait()

        self.assertEqual(receiver.returncode, 0)
        self.assertEqual(sender.returncode, 0)
        f = open(actual.absolute(), 'rb')
        actual_text = f.read()
        f.close()
        self.assertEqual(expected_text, actual_text)
        actual_info = os.stat(actual.absolute())
        self.assertTrue(stat.S_ISREG(actual_info.st_mode))
        self.assertEqual(actual_info.st_mode & stat.S_IREAD, stat.S_IREAD)
        self.assertEqual(actual_info.st_mode & stat.S_IWRITE, stat.S_IWRITE)
        self.assertEqual(actual_info.st_mode & stat.S_IEXEC, 0)
        # I do not think Windows has a concept of group and other for file
        # permissions so these would always fail.
        if (os.name == 'posix'):
            self.assertEqual(actual_info.st_mode & stat.S_IRGRP, 0)
            self.assertEqual(actual_info.st_mode & stat.S_IWGRP, 0)
            self.assertEqual(actual_info.st_mode & stat.S_IXGRP, 0)
            self.assertEqual(actual_info.st_mode & stat.S_IROTH, 0)
            self.assertEqual(actual_info.st_mode & stat.S_IWOTH, 0)
            self.assertEqual(actual_info.st_mode & stat.S_IXOTH, 0)

        dir.cleanup()

    async def test_incp_src_file_dest_file_does_not_exist(self):
        '''
        POSIX 3.b

        It should create the destination file and copy the source file when the
        destination file does not exist. It should set the permission bits of
        dest file to the same as source file.
        '''
        expected_text = b'hello, world\n\nSome test \t\t\t TEXT\n'
        dir = tempfile.TemporaryDirectory()
        expected = Path.joinpath(Path(dir.name), 'expected.txt')
        actual = Path.joinpath(Path(dir.name), 'actual.txt')
        f = open(expected.absolute(), 'wb')
        f.write(expected_text)
        f.close()
        os.chmod(expected.absolute(), stat.S_IREAD)


        receiver = await asyncio.create_subprocess_exec('./incp', '-l')
        await asyncio.sleep(0.5)
        sender = await asyncio.create_subprocess_exec('./incp', expected.absolute(), f"127.0.0.1:{actual.absolute()}")
        await receiver.wait()
        await sender.wait()
        f = open(actual.absolute(), 'rb')
        actual_text = f.read()
        f.close()

        self.assertEqual(0, receiver.returncode)
        self.assertEqual(0, sender.returncode)
        self.assertEqual(expected_text, actual_text)
        expected_info = os.stat(expected.absolute())
        actual_info = os.stat(actual.absolute())
        self.assertEqual(expected_info.st_mode, actual_info.st_mode)

        dir.cleanup()

    async def test_incp_src_file_dest_file_cannot_open(self):
        '''
        POSIX 3.c

        It should write an error message and skip the source file when
        destination file cannot be opened.
        '''
        self.skipTest('not implemented')

    # TODO: How to test this? Does this even need to be a test case?
    # async def test_incp_src_file_dest_file_closes_file(self):
    #     '''
    #     POSIX 3.e

    #     It should close the destination file when it is done copying.
    #     '''
    #     self.skipTest('not implemented')

    # TODO: Tests for -R.

if __name__ == '__main__':
    unittest.main()
