const userController = require('../controllers/user-controller');
const { body } = require('express-validator');
const authMiddleware = require('../middlewares/auth-middleware');
const projectController = require('../controllers/project-controller');
const taskController = require('../controllers/task-controller');
const commentController = require('../controllers/comment-controller');

const Router = require('express').Router;
const router = new Router();

// Route for root
router.get('/', (req, res) => {
    res.send('API is working!'); // Response to GET /
});

// User routes
router.post('/user/registration',
    body('email').isEmail(),
    body('password').isLength({ min: 6, max: 24 }),
    userController.registration);
    
router.post('/user/login', userController.login);
router.post('/user/logout', userController.logout);
router.post('/user/refresh', userController.updateEmail);

// Project routes
router.post('/project/create', authMiddleware, projectController.createProject);
router.get('/project', projectController.getProject);
router.put('/project/update', authMiddleware, projectController.updateProject);
router.post('/project/delete', authMiddleware, projectController.deleteProject);

// Task routes
router.post('/project/tasks/get', authMiddleware, taskController.getTasks);
router.put('/project/tasks/update', authMiddleware, taskController.updateTask);
router.post('/project/tasks/create', authMiddleware, taskController.createTask);
router.post('/project/tasks/delete', authMiddleware, taskController.deleteTask);

// Comment routes
router.post('/project/comments/create', authMiddleware, commentController.create);
router.post('/project/comments/delete', authMiddleware, commentController.delete);

module.exports = router;
