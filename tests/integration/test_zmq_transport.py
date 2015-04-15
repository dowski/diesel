import collections

import zmq

from diesel.transports.zeromq import ZeroMQContext
from mock import Mock, patch


NO_MORE_EVENTS = 0
FAKE_FD = 1
SIMULATE_CLOSE = 'simulate_close'


class TestZeroMQContextEventHandling(object):
    def setup(self):
        self.patcher = patch('diesel.runtime.current_app.hub')
        self.patcher.start()

    def teardown(self):
        self.patcher.stop()

    def test_unregisters_from_hub_when_zeromq_closed(self):
        zsock = Mock()
        zsock.closed = True
        mock_getsockopt(zsock, FAKE_FD)
        s = ZeroMQContext(zsock, "fake://addr")
        s.handle_events()

        zsock.getsockopt.assert_called_once_with(zmq.FD)
        s.hub.unregister.assert_called_once_with(s.sock)

    def test_receives_from_zmq_on_read_event(self):
        zsock = Mock()
        zsock.closed = False
        mock_getsockopt(zsock, FAKE_FD, zmq.POLLIN, NO_MORE_EVENTS)
        s = ZeroMQContext(zsock, "fake://addr")
        s.handle_events()

        zsock.recv.assert_called_once_with(zmq.NOBLOCK)

    def test_sends_to_zmq_on_write_events(self):
        zsock = Mock()
        zsock.closed = False
        mock_getsockopt(zsock, FAKE_FD, zmq.POLLOUT, NO_MORE_EVENTS)
        s = ZeroMQContext(zsock, "fake://addr")
        s.outgoing.append('foo')
        s.handle_events()

        zsock.send.assert_called_once_with('foo', zmq.NOBLOCK)

    def test_sends_and_receives_on_combined_event(self):
        zsock = Mock()
        zsock.closed = False
        mock_getsockopt(
                zsock, FAKE_FD, zmq.POLLOUT | zmq.POLLIN, NO_MORE_EVENTS)
        s = ZeroMQContext(zsock, "fake://addr")
        s.outgoing.append('foo')
        s.handle_events()

        zsock.send.assert_called_once_with('foo', zmq.NOBLOCK)
        zsock.recv.assert_called_once_with(zmq.NOBLOCK)
        assert not s.hub.unregister.called

    def test_handles_close_during_event_handling_by_unregistering(self):
        zsock = Mock()
        zsock.closed = False
        mock_getsockopt(
                zsock, FAKE_FD, SIMULATE_CLOSE, zmq.POLLOUT | zmq.POLLIN,
                NO_MORE_EVENTS)
        s = ZeroMQContext(zsock, "fake://addr")
        s.outgoing.append('foo')
        s.handle_events()

        zsock.send.assert_called_once_with('foo', zmq.NOBLOCK)
        zsock.recv.assert_called_once_with(zmq.NOBLOCK)
        s.hub.unregister.assert_called_once_with(s.sock)

    def test_requeues_message_for_delivery_when_EAGAIN_on_send(self):
        zsock = Mock()
        zsock.closed = False
        zsock.send = Mock(side_effect=zmq.ZMQError(zmq.EAGAIN))
        mock_getsockopt(zsock, FAKE_FD, zmq.POLLOUT, NO_MORE_EVENTS)
        s = ZeroMQContext(zsock, "fake://addr")
        s.outgoing.append('foo')
        s.handle_events()

        zsock.send.assert_called_once_with('foo', zmq.NOBLOCK)
        assert s.outgoing == collections.deque(['foo']), s.outgoing


def mock_getsockopt(zsock, *return_vals):
    return_vals = list(return_vals)
    def side_effect(*ignored):
        val = return_vals.pop(0)
        if val == SIMULATE_CLOSE:
            val = return_vals.pop(0)
            zsock.closed = True
        return val
    zsock.getsockopt = Mock(side_effect=side_effect)


